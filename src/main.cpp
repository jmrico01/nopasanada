#include <cassert>
#include <filesystem>
#include <httplib.h>
#include <stdio.h>

#include <km_common/km_defines.h>
#include <km_common/km_kmkv.h>
#include <km_common/km_lib.h>
#include <km_common/km_os.h>
#include <km_common/km_string.h>

global_var const int HTTP_STATUS_ERROR = 500;

global_var const int SERVER_PORT = 6060;

template <typename Allocator>
internal bool SearchReplaceAndAppend(const Array<char>& string, const HashTable<Array<char>>& items,
	DynamicArray<char, Allocator>* outString)
{
	uint64 i = 0;
	bool oneBracket = false;
	bool insideBracket = false;
	HashKey replaceKey;
	while (i < string.size) {
		if (insideBracket) {
			if (string[i] == '}') {
				if (oneBracket) {
					const Array<char>* replaceValuePtr = items.GetValue(replaceKey);
					if (replaceValuePtr == nullptr) {
						fprintf(stderr, "Failed to find replace key %.*s\n",
							(int)replaceKey.string.size, replaceKey.string.data);
						return false;
					}
					outString->Append(*replaceValuePtr);

					insideBracket = false;
					oneBracket = false;
				}
				else {
					oneBracket = true;
				}
				i++;
				continue;
			}
			else if (oneBracket) {
				oneBracket = false;
				replaceKey.string.Append('}');
			}
			if (IsWhitespace(string[i])) {
				i++;
				continue;
			}

			replaceKey.string.Append(string[i++]);
			continue;
		}
		else if (string[i] == '{') {
			if (oneBracket) {
				replaceKey.string.Clear();
				insideBracket = true;
				oneBracket = false;
			}
			else {
				oneBracket = true;
			}
			i++;
			continue;
		}
		else if (oneBracket) {
			oneBracket = false;
			outString->Append('{');
		}

		outString->Append(string[i++]);
	}

	return true;
}

void AllocAndSetString(KmkvItem<StandardAllocator>* item, const Array<char>& string)
{
	item->isString = true;
	item->dynamicStringPtr = defaultAllocator_.template New<DynamicArray<char, StandardAllocator>>();
	new (item->dynamicStringPtr) DynamicArray<char, StandardAllocator>(string);
}

bool LoadAllMetadataJson(const Array<char>& rootPath, DynamicArray<char, StandardAllocator>* outJson)
{
	outJson->Append('[');

	FixedArray<char, PATH_MAX_LENGTH> pathBuffer;
	pathBuffer.Clear();
	pathBuffer.Append(rootPath);
	pathBuffer.Append(ToString("data/content"));
	pathBuffer.Append('\0');
	for (const auto& entryIt : std::filesystem::recursive_directory_iterator(pathBuffer.data)) {
		if (!entryIt.is_regular_file()) {
			continue;
		}

		const std::filesystem::path::value_type* dirPath = entryIt.path().c_str();
		pathBuffer.Clear();
		// Oof, hacky and I love it. Handles wchar_t on Windows.
		while (*dirPath != '\0') {
			pathBuffer.Append((char)(*(dirPath++)));
		}
		for (uint64 i = 0; i < pathBuffer.size; i++) {
			if (pathBuffer[i] == '\\') {
				pathBuffer[i] = '/';
			}
		}

		HashTable<KmkvItem<StandardAllocator>> entryKmkv;
		if (!LoadKmkv(pathBuffer.ToArray(), &defaultAllocator_, &entryKmkv)) {
			fprintf(stderr, "LoadKmkv failed for entry %.*s\n",
				(int)pathBuffer.size, pathBuffer.data);
			return false;
		}

		HashTable<KmkvItem<StandardAllocator>> metadataKmkv;
		const auto* entryType = GetKmkvItemStrValue(entryKmkv, "type");
		if (entryType == nullptr) {
			fprintf(stderr, "Entry missing \"type\": %.*s\n",
				(int)pathBuffer.size, pathBuffer.data);
			return false;
		}
		AllocAndSetString(metadataKmkv.Add("type"), entryType->ToArray());

		const auto* entryTags = GetKmkvItemStrValue(entryKmkv, "tags");
		if (entryTags == nullptr) {
			fprintf(stderr, "Entry missing \"tags\": %.*s\n",
				(int)pathBuffer.size, pathBuffer.data);
			return false;
		}
		AllocAndSetString(metadataKmkv.Add("tags"), Array<char>::empty);
		auto& tagsString = *(metadataKmkv.GetValue("tags")->dynamicStringPtr);
		tagsString.Append('[');
		tagsString.Append('"');
		for (uint64 i = 0; i < entryTags->size; i++) {
			char c = (*entryTags)[i];
			if (c == ' ') {
				continue;
			}
			if (c == ',') {
				tagsString.Append('"');
			}
			tagsString.Append(c);
			if (c == ',') {
				tagsString.Append('"');
			}
		}
		tagsString.Append('"');
		tagsString.Append(']');
		metadataKmkv.GetValue("tags")->keywordTag.Append(ToString("array"));

		uint64 start = SubstringSearch(pathBuffer.ToArray(), ToString("content"));
		if (start == pathBuffer.size) {
			fprintf(stderr, "Couldn't find \"content\" substring in path\n");
			return false;
		}
		Array<char> link = pathBuffer.ToArray().Slice(start - 1, pathBuffer.size - 5);
		AllocAndSetString(metadataKmkv.Add("link"), link);

		const auto* entryTitle = GetKmkvItemStrValue(entryKmkv, "title");
		if (entryTitle == nullptr) {
			fprintf(stderr, "Entry missing \"title\": %.*s\n",
				(int)pathBuffer.size, pathBuffer.data);
			return false;
		}
		AllocAndSetString(metadataKmkv.Add("title"), entryTitle->ToArray());

		const auto* entryFeaturedKmkv = GetKmkvItemObjValue(entryKmkv, "featured");
		if (entryFeaturedKmkv == nullptr) {
			fprintf(stderr, "Entry missing \"featured\": %.*s\n",
				(int)pathBuffer.size, pathBuffer.data);
			return false;
		}
		auto* featured = metadataKmkv.Add("featuredInfo");
		featured->isString = false;
		featured->hashTablePtr = defaultAllocator_.template New<HashTable<KmkvItem<StandardAllocator>>>();
		new (featured->hashTablePtr) HashTable<KmkvItem<StandardAllocator>>();
		auto& featuredKmkv = *featured->hashTablePtr;
		const auto* entryFeaturedPretitle = GetKmkvItemStrValue(*entryFeaturedKmkv, "pretitle");
		if (entryFeaturedPretitle != nullptr) {
			AllocAndSetString(featuredKmkv.Add("pretitle"), entryFeaturedPretitle->ToArray());
		}
		const auto* entryFeaturedTitle = GetKmkvItemStrValue(*entryFeaturedKmkv, "title");
		if (entryFeaturedTitle != nullptr) {
			AllocAndSetString(featuredKmkv.Add("title"), entryFeaturedTitle->ToArray());
		}
		const auto* entryFeaturedText1 = GetKmkvItemStrValue(*entryFeaturedKmkv, "text1");
		if (entryFeaturedText1 != nullptr) {
			AllocAndSetString(featuredKmkv.Add("text1"), entryFeaturedText1->ToArray());
		}
		const auto* entryFeaturedText2 = GetKmkvItemStrValue(*entryFeaturedKmkv, "text2");
		if (entryFeaturedText2 != nullptr) {
			AllocAndSetString(featuredKmkv.Add("text2"), entryFeaturedText2->ToArray());
		}
		const auto* entryFeaturedColor = GetKmkvItemStrValue(*entryFeaturedKmkv, "highlightColor");
		if (entryFeaturedColor != nullptr) {
			AllocAndSetString(featuredKmkv.Add("highlightColor"), entryFeaturedColor->ToArray());
		}

		const auto* entryMediaKmkv = GetKmkvItemObjValue(entryKmkv, "media");
		if (entryMediaKmkv == nullptr) {
			fprintf(stderr, "Entry missing \"media\": %.*s\n",
				(int)pathBuffer.size, pathBuffer.data);
			return false;
		}
		const auto* mediaHeader = GetKmkvItemStrValue(*entryMediaKmkv, "header");
		if (mediaHeader == nullptr) {
			fprintf(stderr, "Entry media missing \"header\": %.*s\n",
				(int)pathBuffer.size, pathBuffer.data);
			return false;
		}
		const auto* mediaPoster = GetKmkvItemStrValue(*entryMediaKmkv, "poster");
		if (mediaPoster == nullptr) {
			AllocAndSetString(metadataKmkv.Add("image"), mediaHeader->ToArray());
		}
		else {
			AllocAndSetString(metadataKmkv.Add("image"), mediaPoster->ToArray());
		}

		// TODO look up "featured1" ... "featuredN" in entry media and use that if present
		AllocAndSetString(featuredKmkv.Add("images"), Array<char>::empty);
		auto& featuredImagesString = *(featuredKmkv.GetValue("images")->dynamicStringPtr);
		featuredImagesString.Append('[');
		featuredImagesString.Append('"');
		featuredImagesString.Append(mediaHeader->ToArray());
		featuredImagesString.Append('"');
		featuredImagesString.Append(']');
		featuredKmkv.GetValue("images")->keywordTag.Append(ToString("array"));

		if (!KmkvToJson(metadataKmkv, outJson)) {
			fprintf(stderr, "KmkvToJson failed for entry %.*s\n",
				(int)pathBuffer.size, pathBuffer.data);
			return false;
		}
		outJson->Append(',');
	}

	outJson->RemoveLast();
	outJson->Append(']');
	return true;
}

int main(int argc, char** argv)
{
	httplib::Server httpServer;

	FixedArray<char, PATH_MAX_LENGTH> rootPath = GetExecutablePath(&defaultAllocator_);
	if (rootPath.size == 0) {
		fprintf(stderr, "Failed to get executable path\n");
		return 1;
	}
	rootPath.RemoveLast();
	rootPath.size = GetLastOccurrence(rootPath.ToArray(), '/');
	printf("Root path: %.*s\n", (int)rootPath.size, rootPath.data);

	FixedArray<char, PATH_MAX_LENGTH> mediaKmkvPath = rootPath;
	mediaKmkvPath.Append(ToString("data/media.kmkv"));
	HashTable<KmkvItem<StandardAllocator>> mediaKmkv;
	if (!LoadKmkv(mediaKmkvPath.ToArray(), &defaultAllocator_, &mediaKmkv)) {
		fprintf(stderr, "LoadKmkv failed for media file\n");
		return 1;
	}

	FixedArray<char, PATH_MAX_LENGTH> publicPath = rootPath;
	publicPath.Append(ToString("data/public"));
	publicPath.Append('\0');
	bool result = httpServer.set_base_dir(publicPath.data);
	if (!result) {
		fprintf(stderr, "set_base_dir failed on dir %s\n", publicPath.data);
		return 1;
	}

	DynamicArray<char> allMetadataJson;
	if (!LoadAllMetadataJson(rootPath.ToArray(), &allMetadataJson)) {
		fprintf(stderr, "Failed to load all entry metadata to JSON\n");
		return 1;
	}
	printf("all metadata:\n%.*s\n", (int)allMetadataJson.size, allMetadataJson.data);

	// Backwards compatibility =====================================================================
	httpServer.Get("/el-caso-diet-prada", [](const httplib::Request& req, httplib::Response& res) {
		res.set_redirect("/content/201908/el-caso-diet-prada");
	});
	httpServer.Get("/la-cerveza-si-es-cosa-de-mujeres", [](const httplib::Request& req, httplib::Response& res) {
		res.set_redirect("/content/201908/la-cerveza-si-es-cosa-de-mujeres");
	});
	httpServer.Get("/content/201908/el-amazonas", [](const httplib::Request& req, httplib::Response& res) {
		res.set_redirect("/content/201908/newsletter-29");
	});
	httpServer.Get("/content/201909/newsletter-03", [](const httplib::Request& req, httplib::Response& res) {
		res.set_redirect("/content/201909/newsletter-03");
	});
	// =============================================================================================

	httpServer.Get("/entries", [&allMetadataJson](const httplib::Request& req, httplib::Response& res) {
		res.set_content(allMetadataJson.data, allMetadataJson.size, "application/json");
	});

	httpServer.Get("/featured", [&allMetadataJson](const httplib::Request& req, httplib::Response& res) {
		res.set_content("{\"home\":\"/content/202001/15-camaras-trampa\",\"noticias\":\"/content/202001/15-lo-importante\",\"ciencia\":\"/content/202001/15-camaras-trampa\",\"cultura\":\"/content/202001/09-hildur-guonadottir\",\"opinion\":\"/content/202001/09-hildur-guonadottir\",\"deporte\":\"/content/201912/17-me-mejor-experiencia\",\"nopasanada\":\"/content/201908/nopasanada\"}", "application/json");
	});

	httpServer.Get("/content/[^/]+/.+", [rootPath, &mediaKmkv](const httplib::Request& req, httplib::Response& res) {
		Array<char> requestPath = ToString(req.path.c_str());
		if (requestPath[requestPath.size - 1] == '/') {
			requestPath.RemoveLast();
		}

		FixedArray<char, PATH_MAX_LENGTH> kmkvPath = rootPath;
		kmkvPath.Append(ToString("data"));
		kmkvPath.Append(requestPath);
		kmkvPath.Append(ToString(".kmkv"));
		HashTable<KmkvItem<StandardAllocator>> kmkv;
		if (!LoadKmkv(kmkvPath.ToArray(), &defaultAllocator_, &kmkv)) {
			fprintf(stderr, "LoadKmkv failed for %.*s\n", (int)kmkvPath.size, kmkvPath.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}

		const auto* type = GetKmkvItemStrValue(kmkv, "type");
		if (type == nullptr) {
			fprintf(stderr, "Entry missing string \"type\": %.*s\n",
				(int)kmkvPath.size, kmkvPath.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}

		FixedArray<char, PATH_MAX_LENGTH> templatePath = rootPath;
		templatePath.Append(ToString("data/templates/"));
		templatePath.Append(type->ToArray());
		templatePath.Append(ToString(".html"));
		templatePath.Append('\0');
		Array<uint8> templateFile = LoadEntireFile(templatePath.ToArray(), &defaultAllocator_);
		if (templateFile.data == nullptr) {
			fprintf(stderr, "Failed to load template file at %.*s\n",
				(int)templatePath.size, templatePath.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		defer(FreeFile(templateFile, &defaultAllocator_));

		HashTable<Array<char>> templateItems;
		templateItems.Add("url", requestPath);

		const auto* media = GetKmkvItemObjValue(kmkv, "media");
		if (media == nullptr) {
			fprintf(stderr, "Entry missing kmkv object \"media\": %.*s\n",
				(int)kmkvPath.size, kmkvPath.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		const auto* header = GetKmkvItemStrValue(*media, "header");
		if (header == nullptr) {
			fprintf(stderr, "Entry media missing string \"header\": %.*s\n",
				(int)kmkvPath.size, kmkvPath.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		templateItems.Add("image", header->ToArray());

		if (StringCompare(type->ToArray(), "newsletter")) {
			const char* NEWSLETTER_IMAGE_FIELDS[] = {
				"header-desktop1", "header-desktop2", "header-desktop3", "header-desktop4",
				"header-mobile1",  "header-mobile2",  "header-mobile3",  "header-mobile4"
			};
			for (int i = 0; i < 8; i++) {
				const auto* image = GetKmkvItemStrValue(*media, NEWSLETTER_IMAGE_FIELDS[i]);
				if (image == nullptr) {
					fprintf(stderr, "Entry media missing string \"%s\": %.*s\n",
						NEWSLETTER_IMAGE_FIELDS[i], (int)kmkvPath.size, kmkvPath.data);
					res.status = HTTP_STATUS_ERROR;
					return;
				}
				templateItems.Add(NEWSLETTER_IMAGE_FIELDS[i], image->ToArray());
			}
		}

		const auto* day = GetKmkvItemStrValue(kmkv, "day");
		if (day == nullptr) {
			fprintf(stderr, "Entry missing string \"day\": %.*s\n",
				(int)kmkvPath.size, kmkvPath.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		const auto* month = GetKmkvItemStrValue(kmkv, "month");
		if (month == nullptr) {
			fprintf(stderr, "Entry missing string \"month\": %.*s\n",
				(int)kmkvPath.size, kmkvPath.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		const auto* year = GetKmkvItemStrValue(kmkv, "year");
		if (year == nullptr) {
			fprintf(stderr, "Entry missing string \"year\": %.*s\n",
				(int)kmkvPath.size, kmkvPath.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}

		const uint64 DATE_STRING_MAX_LENGTH = 64;
		FixedArray<char, DATE_STRING_MAX_LENGTH> dateString;
		dateString.Clear();
		if (day->size == 1) {
			dateString.Append('0');
		}
		else if (day->size != 2) {
			fprintf(stderr, "Entry bad day string length %d: %.*s\n", (int)day->size,
				(int)kmkvPath.size, kmkvPath.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		dateString.Append(day->ToArray());
		dateString.Append(ToString(" DE "));
		int monthInt;
		if (!StringToIntBase10(month->ToArray(), &monthInt)) {
			fprintf(stderr, "Entry month to-integer conversion failed: %.*s\n",
				(int)kmkvPath.size, kmkvPath.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		if (monthInt < 1 || monthInt > 12) {
			fprintf(stderr, "Entry month %d out of range: %.*s\n", monthInt,
				(int)kmkvPath.size, kmkvPath.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		const char* monthNames[] = {
			"ENERO", "FEBRERO", "MARZO",
			"ABRIL", "MAYO", "JUNIO",
			"JULIO", "AGOSTO", "SEPTIEMBRE",
			"OCTUBRE", "NOVIEMBRE", "DICIEMBRE"
		};
		dateString.Append(ToString(monthNames[monthInt - 1]));
		templateItems.Add("subtextRight", dateString.ToArray());

		if (StringCompare(type->ToArray(), "newsletter")) {
			templateItems.Add("subtextRight1", dateString.ToArray());
			templateItems.Add("subtextRight2", dateString.ToArray());
			templateItems.Add("subtextRight3", dateString.ToArray());
			templateItems.Add("subtextRight4", dateString.ToArray());
		}

		const auto* description = GetKmkvItemStrValue(kmkv, "description");
		if (description == nullptr) {
			templateItems.Add("description", Array<char>::empty);
		}
		else {
			templateItems.Add("description", description->ToArray());
		}

		const auto* color = GetKmkvItemStrValue(kmkv, "color");
		if (color == nullptr) {
			fprintf(stderr, "Entry missing string \"color\": %.*s\n",
				(int)kmkvPath.size, kmkvPath.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		templateItems.Add("color", color->ToArray());

		const auto* title = GetKmkvItemStrValue(kmkv, "title");
		if (title == nullptr) {
			fprintf(stderr, "Entry missing string \"title\": %.*s\n",
				(int)kmkvPath.size, kmkvPath.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		templateItems.Add("title", title->ToArray());

		if (StringCompare(type->ToArray(), "newsletter")) {
			const char* TITLE_STRINGS[4] = {
				"title1", "title2", "title3", "title4"
			};
			for (int i = 0; i < 4; i++) {
				const auto* titleNL = GetKmkvItemStrValue(kmkv, TITLE_STRINGS[i]);
				if (titleNL == nullptr) {
					fprintf(stderr, "Entry missing string \"%s\": %.*s\n", TITLE_STRINGS[i],
						(int)kmkvPath.size, kmkvPath.data);
					res.status = HTTP_STATUS_ERROR;
					return;
				}
				templateItems.Add(TITLE_STRINGS[i], titleNL->ToArray());
			}

			const auto* customTop = GetKmkvItemStrValue(kmkv, "customTop");
			if (customTop == nullptr) {
				templateItems.Add("customTop", Array<char>::empty);
			}
			else {
				templateItems.Add("customTop", customTop->ToArray());
			}
		}
		else {
			const auto* subtitle = GetKmkvItemStrValue(kmkv, "subtitle");
			if (subtitle == nullptr) {
				templateItems.Add("subtitle", Array<char>::empty);
			}
			else {
				templateItems.Add("subtitle", subtitle->ToArray());
			}
		}

		FixedArray<const char*, 8> AUTHOR_STRINGS_NEWSLETTER;
		AUTHOR_STRINGS_NEWSLETTER.Clear();
		AUTHOR_STRINGS_NEWSLETTER.Append("author1");
		AUTHOR_STRINGS_NEWSLETTER.Append("subtextLeft1");
		AUTHOR_STRINGS_NEWSLETTER.Append("author2");
		AUTHOR_STRINGS_NEWSLETTER.Append("subtextLeft2");
		AUTHOR_STRINGS_NEWSLETTER.Append("author3");
		AUTHOR_STRINGS_NEWSLETTER.Append("subtextLeft3");
		AUTHOR_STRINGS_NEWSLETTER.Append("author4");
		AUTHOR_STRINGS_NEWSLETTER.Append("subtextLeft4");
		FixedArray<const char*, 2> AUTHOR_STRINGS_OTHER;
		AUTHOR_STRINGS_OTHER.Clear();
		AUTHOR_STRINGS_OTHER.Append("author");
		AUTHOR_STRINGS_OTHER.Append("subtextLeft");

		Array<const char*> authorStrings = AUTHOR_STRINGS_OTHER.ToArray();
		if (StringCompare(type->ToArray(), "newsletter")) {
			authorStrings = AUTHOR_STRINGS_NEWSLETTER.ToArray();
		}

		for (uint64 i = 0; i < authorStrings.size / 2; i++) {
			const auto* author = GetKmkvItemStrValue(kmkv, authorStrings[i * 2]);
			if (author == nullptr) {
				templateItems.Add(authorStrings[i * 2 + 1], Array<char>::empty);
				continue;
			}
			const uint64 AUTHOR_STRING_MAX_LENGTH = 64;
			FixedArray<char, AUTHOR_STRING_MAX_LENGTH> authorString;
			authorString.Clear();
			authorString.Append(ToString("POR "));
			DynamicArray<char> authorUpper;
			if (!Utf8ToUppercase(author->ToArray(), &authorUpper)) {
				fprintf(stderr, "Entry author to-upper failed: %.*s\n",
					(int)kmkvPath.size, kmkvPath.data);
				res.status = HTTP_STATUS_ERROR;
				return;
			}
			authorString.Append(authorUpper.ToArray());
			templateItems.Add(authorStrings[i * 2 + 1], authorString.ToArray());
		}

		if (StringCompare(type->ToArray(), "newsletter")) {
			const char* TEXT_FIELD_NAMES[4] = { "text1", "text2", "text3", "text4" };
			for (int i = 0; i < 4; i++) {
				const auto* text = GetKmkvItemStrValue(kmkv, TEXT_FIELD_NAMES[i]);
				if (text == nullptr) {
					fprintf(stderr, "Entry missing string \"%s\": %.*s\n", TEXT_FIELD_NAMES[i],
						(int)kmkvPath.size, kmkvPath.data);
					res.status = HTTP_STATUS_ERROR;
					return;
				}
				templateItems.Add(TEXT_FIELD_NAMES[i], text->ToArray());
			}
		}
		else {
			const auto* text = GetKmkvItemStrValue(kmkv, "text");
			if (text == nullptr) {
				fprintf(stderr, "Entry missing string \"text\": %.*s\n",
					(int)kmkvPath.size, kmkvPath.data);
				res.status = HTTP_STATUS_ERROR;
				return;
			}
			templateItems.Add("text", text->ToArray());
		}

		if (StringCompare(type->ToArray(), "video")) {
			const auto* videoId = GetKmkvItemStrValue(kmkv, "videoID");
			if (videoId == nullptr) {
				fprintf(stderr, "Entry missing string \"videoID\": %.*s\n",
					(int)kmkvPath.size, kmkvPath.data);
				res.status = HTTP_STATUS_ERROR;
				return;
			}
			templateItems.Add("videoID", videoId->ToArray());
		}

		Array<char> templateString;
		templateString.data = (char*)templateFile.data;
		templateString.size = templateFile.size;
		DynamicArray<char> outString;
		if (!SearchReplaceAndAppend(templateString, templateItems, &outString)) {
			fprintf(stderr, "Failed to search-and-replace to template file %.*s\n",
				(int)templatePath.size, templatePath.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}

		const uint64 MEDIA_TYPE_MAX = 12;
		const uint64 MEDIA_NAME_MAX = 32;
		DynamicArray<char> outStringMedia;
		uint64 i = 0;
		while (i < outString.size) {
			if (outString[i] == '$') {
				uint64 j = i + 1;
				while (j < outString.size && outString[j] != '/' && j - i - 1 < MEDIA_TYPE_MAX) {
					j++;
				}
				if (outString[j] != '/') {
					i++;
					continue;
				}
				Array<char> mediaType = outString.ToArray().Slice(i + 1, j);

				uint64 nameStart = ++j;
				while (j < outString.size && outString[j] != '$' && j - nameStart < MEDIA_NAME_MAX) {
					j++;
				}
				if (outString[j] != '$') {
					i++;
					continue;
				}
				Array<char> mediaName = outString.ToArray().Slice(nameStart, j);
				i = j + 1;

				const auto* mediaHtml = GetKmkvItemStrValue(mediaKmkv, mediaType);
				if (mediaHtml == nullptr) {
					fprintf(stderr, "Media type not found: %.*s in %.*s\n",
						(int)mediaType.size, mediaType.data, (int)kmkvPath.size, kmkvPath.data);
					res.status = HTTP_STATUS_ERROR;
					return;
				}

				HashTable<Array<char>> mediaHtmlItems;
				if (StringCompare(mediaType, "image") || StringCompare(mediaType, "imageHalfWidth")) {
					// TODO if there are non-image media things in the future, this will need to be
					// expanded upon / keyword tag needs to be checked for type=image
					const auto* imageLocation = GetKmkvItemStrValue(*media, mediaName);
					if (imageLocation == nullptr) {
						fprintf(stderr, "Image not found: %.*s in %.*s\n",
							(int)mediaName.size, mediaName.data, (int)kmkvPath.size, kmkvPath.data);
						res.status = HTTP_STATUS_ERROR;
						return;
					}
					mediaHtmlItems.Add("location", imageLocation->ToArray());
				}
				else {
					mediaHtmlItems.Add("location", mediaName);
				}
				// TODO implement style extraction from KMKV (multiple keyword tag support)
				mediaHtmlItems.Add("style", Array<char>::empty);
				if (!SearchReplaceAndAppend(mediaHtml->ToArray(), mediaHtmlItems, &outStringMedia)) {
					fprintf(stderr, "Failed to search-and-replace media HTML, type %.*s in %.*s\n",
						(int)mediaType.size, mediaType.data, (int)kmkvPath.size, kmkvPath.data);
					res.status = HTTP_STATUS_ERROR;
					return;
				}
				continue;
			}

			outStringMedia.Append(outString[i++]);
		}

		res.set_content(outStringMedia.data, outStringMedia.size, "text/html");
	});

	printf("Listening on port %d\n", SERVER_PORT);
	httpServer.listen("localhost", SERVER_PORT);

	return 0;
}

#undef GAME_SLOW
#undef GAME_INTERNAL
#include <km_common/km_debug.h>
#undef DEBUG_ASSERTF
#undef DEBUG_ASSERT
#undef DEBUG_PANIC
#define DEBUG_ASSERTF(expression, format, ...) if (!(expression)) { \
	fprintf(stderr, "Assert failed:\n"); \
	fprintf(stderr, format, ##__VA_ARGS__); \
	abort(); }
#define DEBUG_ASSERT(expression) DEBUG_ASSERTF(expression, "nothing")
#define DEBUG_PANIC(format, ...) \
	fprintf(stderr, "PANIC!\n"); \
	fprintf(stderr, format, ##__VA_ARGS__); \
	abort();

#define LOG_ERROR(format, ...) fprintf(stderr, format, ##__VA_ARGS__)

#define STB_SPRINTF_IMPLEMENTATION
#include <stb_sprintf.h>

#include <km_common/km_kmkv.cpp>
#include <km_common/km_lib.cpp>
#include <km_common/km_memory.cpp>
#include <km_common/km_os.cpp>
#include <km_common/km_string.cpp>