#include <cassert>
#include <filesystem>
#if SERVER_HTTPS
#define CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_ZLIB_SUPPORT
#endif
#include <httplib.h>
#include <stdio.h>
#include <thread>

#include <km_common/km_defines.h>
#include <km_common/km_kmkv.h>
#include <km_common/km_lib.h>
#include <km_common/km_os.h>
#include <km_common/km_string.h>

global_var const int HTTP_STATUS_ERROR = 500;

#if SERVER_HTTPS

typedef httplib::SSLServer ServerType;
global_var const char* SERVER_CERT   = "/mnt/c/Users/jmric/Documents/Development/ssl/server.crt";
global_var const char* SERVER_KEY    = "/mnt/c/Users/jmric/Documents/Development/ssl/server.key";

#else

typedef httplib::Server ServerType;

#endif

enum class EntryType
{
	ARTICLE = 0,
	NEWSLETTER,
	TEXT,
	VIDEO,

	LAST
};

global_var const char* ENTRY_TYPE_STRINGS[] = {
	"article",
	"newsletter",
	"text",
	"video"
};

struct NewsletterData
{
	DynamicArray<char> customTop;
	DynamicArray<char> titles[4];
	DynamicArray<char> authors[4];
	DynamicArray<char> texts[4];
};

struct EntryData
{
	HashTable<KmkvItem<StandardAllocator>> kmkv;

	DynamicArray<char> featuredPretitle;
	DynamicArray<char> featuredTitle;
	DynamicArray<char> featuredText1;
	DynamicArray<char> featuredText2;
	DynamicArray<char> featuredHighlightColor;

	HashTable<KmkvItem<StandardAllocator>> media;
	DynamicArray<char> header;

	EntryType type;
	DynamicArray<char> typeString;
	DynamicArray<DynamicArray<char>> tags;
	char dayString[2];
	int dayInt;
	char monthString[2];
	int monthInt;
	char yearString[4];
	int yearInt;
	DynamicArray<char> title;
	DynamicArray<char> description;
	DynamicArray<char> color;
	DynamicArray<char> subtitle;
	DynamicArray<char> author;
	DynamicArray<char> text;

	DynamicArray<char> videoID;

	NewsletterData newsletterData;
};

bool LoadEntry(const Array<char>& rootPath, const Array<char>& uri, EntryData* outEntryData)
{
	FixedArray<char, PATH_MAX_LENGTH> kmkvPath;
	kmkvPath.Clear();
	kmkvPath.Append(rootPath);
	kmkvPath.Append(ToString("data"));
	kmkvPath.Append(uri);
	kmkvPath.Append(ToString(".kmkv"));
	if (!LoadKmkv(kmkvPath.ToArray(), &defaultAllocator_, &outEntryData->kmkv)) {
		fprintf(stderr, "LoadKmkv failed for entry %.*s\n", (int)kmkvPath.size, kmkvPath.data);
		return false;
	}

	// Load featured info
	const auto* featuredKmkv = GetKmkvItemObjValue(outEntryData->kmkv, "featured");
	if (featuredKmkv == nullptr) {
		fprintf(stderr, "Entry missing \"featured\": %.*s\n", (int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	const auto* featuredPretitle = GetKmkvItemStrValue(*featuredKmkv, "pretitle");
	if (featuredPretitle != nullptr) {
		outEntryData->featuredPretitle = *featuredPretitle;
	}
	const auto* featuredTitle = GetKmkvItemStrValue(*featuredKmkv, "title");
	if (featuredTitle != nullptr) {
		outEntryData->featuredTitle = *featuredTitle;
	}
	const auto* featuredText1 = GetKmkvItemStrValue(*featuredKmkv, "text1");
	if (featuredText1 != nullptr) {
		outEntryData->featuredText1 = *featuredText1;
	}
	const auto* featuredText2 = GetKmkvItemStrValue(*featuredKmkv, "text2");
	if (featuredText2 != nullptr) {
		outEntryData->featuredText2 = *featuredText2;
	}
	const auto* featuredHighlightColor = GetKmkvItemStrValue(*featuredKmkv, "highlightColor");
	if (featuredHighlightColor != nullptr) {
		outEntryData->featuredHighlightColor = *featuredHighlightColor;
	}

	// Load media
	const auto* mediaKmkv = GetKmkvItemObjValue(outEntryData->kmkv, "media");
	if (mediaKmkv == nullptr) {
		fprintf(stderr, "Entry missing \"media\": %.*s\n", (int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	outEntryData->media = *mediaKmkv;
	const auto* headerImage = GetKmkvItemStrValue(*mediaKmkv, "header");
	if (headerImage == nullptr) {
		fprintf(stderr, "Entry media missing \"header\": %.*s\n", (int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	outEntryData->header = *headerImage;

	// Load entry type string and enum
	const auto* type = GetKmkvItemStrValue(outEntryData->kmkv, "type");
	if (type == nullptr) {
		fprintf(stderr, "Entry missing \"type\": %.*s\n", (int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	outEntryData->type = EntryType::LAST;
	for (uint64 i = 0; i < (uint64)EntryType::LAST; i++) {
		if (StringEquals(type->ToArray(), ToString(ENTRY_TYPE_STRINGS[i]))) {
			outEntryData->type = (EntryType)i;
		}
	}
	if (outEntryData->type == EntryType::LAST) {
		fprintf(stderr, "Entry with invalid \"type\": %.*s\n", (int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	outEntryData->typeString = *type;

	// Load tags string array
	const auto* tags = GetKmkvItemStrValue(outEntryData->kmkv, "tags");
	if (tags == nullptr) {
		fprintf(stderr, "Entry missing \"tags\": %.*s\n", (int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	DynamicArray<char>* currentTag = outEntryData->tags.Append();
	for (uint64 i = 0; i < tags->size; i++) {
		char c = (*tags)[i];
		if (IsWhitespace(c)) {
			continue;
		}
		else if (c == ',') {
			currentTag = outEntryData->tags.Append();
		}
		else {
			currentTag->Append(c);
		}
	}

	// Load date string and integer values
	const auto* day = GetKmkvItemStrValue(outEntryData->kmkv, "day");
	const auto* month = GetKmkvItemStrValue(outEntryData->kmkv, "month");
	const auto* year = GetKmkvItemStrValue(outEntryData->kmkv, "year");
	if (day == nullptr || month == nullptr || year == nullptr) {
		fprintf(stderr, "Entry missing string day, month, or year field(s): %.*s\n",
			(int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	if (day->size == 1) {
		outEntryData->dayString[0] = '0';
		outEntryData->dayString[1] = (*day)[0];
	}
	else if (day->size == 2) {
		outEntryData->dayString[0] = (*day)[0];
		outEntryData->dayString[1] = (*day)[1];
	}
	else {
		fprintf(stderr, "Entry bad day string length %d: %.*s\n", (int)day->size,
			(int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	if (!StringToIntBase10(day->ToArray(), &outEntryData->dayInt)) {
		fprintf(stderr, "Entry day to-integer conversion failed: %.*s\n",
			(int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	if (outEntryData->dayInt < 1 || outEntryData->dayInt > 31) {
		fprintf(stderr, "Entry day %d out of range: %.*s\n", outEntryData->dayInt,
			(int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	if (month->size == 1) {
		outEntryData->monthString[0] = '0';
		outEntryData->monthString[1] = (*month)[0];
	}
	else if (month->size == 2) {
		outEntryData->monthString[0] = (*month)[0];
		outEntryData->monthString[1] = (*month)[1];
	}
	else {
		fprintf(stderr, "Entry bad month string length %d: %.*s\n", (int)month->size,
			(int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	if (!StringToIntBase10(month->ToArray(), &outEntryData->monthInt)) {
		fprintf(stderr, "Entry month to-integer conversion failed: %.*s\n",
			(int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	if (outEntryData->monthInt < 1 || outEntryData->monthInt > 12) {
		fprintf(stderr, "Entry month %d out of range: %.*s\n", outEntryData->monthInt,
			(int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	if (year->size == 4) {
		outEntryData->yearString[0] = (*year)[0];
		outEntryData->yearString[1] = (*year)[1];
		outEntryData->yearString[2] = (*year)[2];
		outEntryData->yearString[3] = (*year)[3];
	}
	else {
		fprintf(stderr, "Entry bad year string length %d: %.*s\n", (int)year->size,
			(int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	if (!StringToIntBase10(year->ToArray(), &outEntryData->yearInt)) {
		fprintf(stderr, "Entry month to-integer conversion failed: %.*s\n",
			(int)kmkvPath.size, kmkvPath.data);
		return false;
	}

	// Load article contents
	const auto* title = GetKmkvItemStrValue(outEntryData->kmkv, "title");
	if (title == nullptr) {
		fprintf(stderr, "Entry missing \"title\": %.*s\n", (int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	outEntryData->title = *title;
	const auto* color = GetKmkvItemStrValue(outEntryData->kmkv, "color");
	if (color == nullptr) {
		fprintf(stderr, "Entry missing \"color\": %.*s\n", (int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	outEntryData->color = *color;
	const auto* description = GetKmkvItemStrValue(outEntryData->kmkv, "description");
	if (description != nullptr) {
		outEntryData->description = *description;
	}
	const auto* subtitle = GetKmkvItemStrValue(outEntryData->kmkv, "subtitle");
	if (subtitle != nullptr) {
		outEntryData->subtitle = *subtitle;
	}
	const auto* author = GetKmkvItemStrValue(outEntryData->kmkv, "author");
	if (author != nullptr) {
		outEntryData->author = *author;
	}
	const auto* text = GetKmkvItemStrValue(outEntryData->kmkv, "text");
	if (text == nullptr && outEntryData->type != EntryType::NEWSLETTER) {
		fprintf(stderr, "Entry missing \"text\": %.*s\n", (int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	else if (text != nullptr) {
		outEntryData->text = *text;
	}

	// Load video ID
	const auto* videoID = GetKmkvItemStrValue(outEntryData->kmkv, "videoID");
	if (videoID == nullptr && outEntryData->type == EntryType::VIDEO) {
		fprintf(stderr, "Entry missing \"videoID\": %.*s\n", (int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	else if (videoID != nullptr) {
		outEntryData->videoID = *videoID;
	}

	// Load newsletter contents
	if (outEntryData->type == EntryType::NEWSLETTER) {
		const auto* customTop = GetKmkvItemStrValue(outEntryData->kmkv, "customTop");
		if (customTop != nullptr) {
			outEntryData->newsletterData.customTop = *customTop;
		}
		const char* TITLE_FIELD_NAMES[4]  = { "title1",  "title2",  "title3",  "title4" };
		const char* AUTHOR_FIELD_NAMES[4] = { "author1", "author2", "author3", "author4" };
		const char* TEXT_FIELD_NAMES[4]   = { "text1",   "text2",   "text3",   "text4" };
		for (int i = 0; i < 4; i++) {
			const auto* titleN = GetKmkvItemStrValue(outEntryData->kmkv, TITLE_FIELD_NAMES[i]);
			if (titleN == nullptr) {
				fprintf(stderr, "Newsletter missing \"%s\": %.*s\n", TITLE_FIELD_NAMES[i],
					(int)kmkvPath.size, kmkvPath.data);
				return false;
			}
			outEntryData->newsletterData.titles[i] = *titleN;
			const auto* authorN = GetKmkvItemStrValue(outEntryData->kmkv, AUTHOR_FIELD_NAMES[i]);
			if (authorN != nullptr) {
				outEntryData->newsletterData.authors[i] = *authorN;
			}
			const auto* textN = GetKmkvItemStrValue(outEntryData->kmkv, TEXT_FIELD_NAMES[i]);
			if (textN == nullptr) {
				fprintf(stderr, "Newsletter missing \"%s\": %.*s\n", TEXT_FIELD_NAMES[i],
					(int)kmkvPath.size, kmkvPath.data);
				return false;
			}
			outEntryData->newsletterData.texts[i] = *textN;
		}
	}

	return true;
}

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

int CompareMetadataDateDescending(const void* p1, const void* p2)
{
	const auto* kmkv1 = *((const HashTable<KmkvItem<StandardAllocator>>**)p1);
	const auto* kmkv2 = *((const HashTable<KmkvItem<StandardAllocator>>**)p2);

	const auto* date1 = GetKmkvItemStrValue(*kmkv1, "date");
	assert(date1 != nullptr);
	const auto* date2 = GetKmkvItemStrValue(*kmkv2, "date");
	assert(date2 != nullptr);
	return StringCompare(date1->ToArray(), date2->ToArray()) * -1;
}

bool LoadAllMetadataJson(const Array<char>& rootPath, DynamicArray<char, StandardAllocator>* outJson)
{
	outJson->Append('[');

	DynamicArray<HashTable<KmkvItem<StandardAllocator>>> metadataKmkvs;

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

		uint64 start = SubstringSearch(pathBuffer.ToArray(), ToString("content"));
		if (start == pathBuffer.size) {
			fprintf(stderr, "Couldn't find \"content\" substring in path\n");
			return false;
		}
		Array<char> uri = pathBuffer.ToArray().Slice(start - 1, pathBuffer.size - 5);
		EntryData entryData;
		if (!LoadEntry(rootPath, uri, &entryData)) {
			fprintf(stderr, "LoadEntry failed for entry %.*s\n", (int)uri.size, uri.data);
			return false;
		}
		defer(FreeKmkv(entryData.kmkv));

		HashTable<KmkvItem<StandardAllocator>>* metadataKmkvPtr = metadataKmkvs.Append();
		HashTable<KmkvItem<StandardAllocator>>& metadataKmkv = *metadataKmkvPtr;
		AllocAndSetString(metadataKmkv.Add("uri"), uri);
		AllocAndSetString(metadataKmkv.Add("type"), entryData.typeString.ToArray());
		AllocAndSetString(metadataKmkv.Add("tags"), Array<char>::empty);
		auto& tagsString = *(metadataKmkv.GetValue("tags")->dynamicStringPtr);
		tagsString.Append('[');
		for (uint64 i = 0; i < entryData.tags.size; i++) {
			tagsString.Append('"');
			tagsString.Append(entryData.tags[i].ToArray());
			tagsString.Append('"');
			tagsString.Append(',');
		}
		if (entryData.tags.size > 0) {
			tagsString.RemoveLast();
		}
		tagsString.Append(']');
		metadataKmkv.GetValue("tags")->keywordTag.Append(ToString("array"));
		AllocAndSetString(metadataKmkv.Add("title"), entryData.title.ToArray());
		DynamicArray<char> dateString;
		dateString.Append(entryData.yearString[0]);
		dateString.Append(entryData.yearString[1]);
		dateString.Append(entryData.yearString[2]);
		dateString.Append(entryData.yearString[3]);
		dateString.Append(entryData.monthString[0]);
		dateString.Append(entryData.monthString[1]);
		dateString.Append(entryData.dayString[0]);
		dateString.Append(entryData.dayString[1]);
		AllocAndSetString(metadataKmkv.Add("date"), dateString.ToArray());

		auto* featured = metadataKmkv.Add("featuredInfo");
		featured->isString = false;
		featured->hashTablePtr = defaultAllocator_.template New<HashTable<KmkvItem<StandardAllocator>>>();
		new (featured->hashTablePtr) HashTable<KmkvItem<StandardAllocator>>();
		auto& featuredKmkv = *featured->hashTablePtr;
		AllocAndSetString(featuredKmkv.Add("pretitle"), entryData.featuredPretitle.ToArray());
		AllocAndSetString(featuredKmkv.Add("title"), entryData.featuredTitle.ToArray());
		AllocAndSetString(featuredKmkv.Add("text1"), entryData.featuredText1.ToArray());
		AllocAndSetString(featuredKmkv.Add("text2"), entryData.featuredText2.ToArray());
		AllocAndSetString(featuredKmkv.Add("highlightColor"), entryData.featuredHighlightColor.ToArray());

		const auto* poster = GetKmkvItemStrValue(entryData.media, "poster");
		if (poster == nullptr) {
			AllocAndSetString(metadataKmkv.Add("image"), entryData.header.ToArray());
		}
		else {
			AllocAndSetString(metadataKmkv.Add("image"), poster->ToArray());
		}

		// TODO look up "featured1" ... "featuredN" in entry media and use that if present
		AllocAndSetString(featuredKmkv.Add("images"), Array<char>::empty);
		auto& featuredImagesString = *(featuredKmkv.GetValue("images")->dynamicStringPtr);
		featuredImagesString.Append('[');
		featuredImagesString.Append('"');
		featuredImagesString.Append(entryData.header.ToArray());
		featuredImagesString.Append('"');
		featuredImagesString.Append(']');
		featuredKmkv.GetValue("images")->keywordTag.Append(ToString("array"));
	}

	DynamicArray<HashTable<KmkvItem<StandardAllocator>>*> metadataKmkvPtrs(metadataKmkvs.size);
	for (uint64 i = 0; i < metadataKmkvs.size; i++) {
		metadataKmkvPtrs.Append(&metadataKmkvs[i]);
	}

	qsort(metadataKmkvPtrs.data, metadataKmkvPtrs.size,
		sizeof(HashTable<KmkvItem<StandardAllocator>>*), CompareMetadataDateDescending);

	for (uint64 i = 0; i < metadataKmkvPtrs.size; i++) {
		if (!KmkvToJson(*metadataKmkvPtrs[i], outJson)) {
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

bool LoadFeaturedJson(const Array<char>& rootPath, DynamicArray<char, StandardAllocator>* outJson)
{
	FixedArray<char, PATH_MAX_LENGTH> featuredKmkvPath;
	featuredKmkvPath.Clear();
	featuredKmkvPath.Append(rootPath);
	featuredKmkvPath.Append(ToString("data/featured.kmkv"));
	featuredKmkvPath.Append('\0');

	HashTable<KmkvItem<StandardAllocator>> featuredKmkv;
	if (!LoadKmkv(featuredKmkvPath.ToArray(), &defaultAllocator_, &featuredKmkv)) {
		fprintf(stderr, "LoadKmkv failed for featured entries: %.*s\n",
			(int)featuredKmkvPath.size, featuredKmkvPath.data);
		return false;
	}
	defer(FreeKmkv(featuredKmkv));

	outJson->Clear();
	if (!KmkvToJson(featuredKmkv, outJson)) {
		fprintf(stderr, "KmkvToJson failed for featured entries: %.*s\n",
			(int)featuredKmkvPath.size, featuredKmkvPath.data);
		return false;
	}

	return true;
}

bool ServerListen(ServerType& server, const char* host, int port)
{
	printf("Listening on host \"%s\", port %d\n", host, port);
	if (!server.listen(host, port)) {
		fprintf(stderr, "server listen failed for host \"%s\", port %d\n", host, port);
		return false;
	}

	return true;
}

int main(int argc, char** argv)
{
#if SERVER_HTTPS
	ServerType server(SERVER_CERT, SERVER_KEY);
#else
	ServerType server;
#endif

#if SERVER_DEV
#if SERVER_HTTPS
	ServerType serverDev(SERVER_CERT, SERVER_KEY);
#else
	ServerType serverDev;
#endif
#endif

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

	DynamicArray<char> allMetadataJson;
	if (!LoadAllMetadataJson(rootPath.ToArray(), &allMetadataJson)) {
		fprintf(stderr, "Failed to load all entry metadata to JSON\n");
		return 1;
	}
	// printf("Metadata JSON:\n%.*s\n", (int)allMetadataJson.size, allMetadataJson.data);

	DynamicArray<char> featuredJson;
	if (!LoadFeaturedJson(rootPath.ToArray(), &featuredJson)) {
		fprintf(stderr, "Failed to load featured entries to JSON\n");
		return 1;
	}

	// Backwards compatibility =====================================================================
	server.Get("/el-caso-diet-prada", [](const httplib::Request& req, httplib::Response& res) {
		res.set_redirect("/content/201908/el-caso-diet-prada");
	});
	server.Get("/la-cerveza-si-es-cosa-de-mujeres", [](const httplib::Request& req, httplib::Response& res) {
		res.set_redirect("/content/201908/la-cerveza-si-es-cosa-de-mujeres");
	});
	server.Get("/content/201908/el-amazonas", [](const httplib::Request& req, httplib::Response& res) {
		res.set_redirect("/content/201908/newsletter-29");
	});
	server.Get("/content/201909/newsletter-03", [](const httplib::Request& req, httplib::Response& res) {
		res.set_redirect("/content/201909/newsletter-03");
	});
	// =============================================================================================

	server.Get("/entries", [&allMetadataJson](const httplib::Request& req, httplib::Response& res) {
		res.set_content(allMetadataJson.data, allMetadataJson.size, "application/json");
	});

	server.Get("/featured", [&featuredJson](const httplib::Request& req, httplib::Response& res) {
		res.set_content(featuredJson.data, featuredJson.size, "application/json");
	});

	server.Get("/content/[^/]+/.+", [rootPath, &mediaKmkv](const httplib::Request& req, httplib::Response& res) {
		Array<char> uri = ToString(req.path.c_str());
		if (uri[uri.size - 1] == '/') {
			uri.RemoveLast();
		}

		EntryData entryData;
		if (!LoadEntry(rootPath.ToArray(), uri, &entryData)) {
			fprintf(stderr, "LoadEntry failed for entry %.*s\n", (int)uri.size, uri.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		defer(FreeKmkv(entryData.kmkv));

		FixedArray<char, PATH_MAX_LENGTH> templatePath = rootPath;
		templatePath.Append(ToString("data/templates/"));
		templatePath.Append(entryData.typeString.ToArray());
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
		templateItems.Add("uri", uri);
		templateItems.Add("image", entryData.header.ToArray());

		if (entryData.type == EntryType::NEWSLETTER) {
			const char* NEWSLETTER_IMAGE_FIELDS[] = {
				"header-desktop1", "header-desktop2", "header-desktop3", "header-desktop4",
				"header-mobile1",  "header-mobile2",  "header-mobile3",  "header-mobile4"
			};
			for (int i = 0; i < 8; i++) {
				const auto* image = GetKmkvItemStrValue(entryData.media, NEWSLETTER_IMAGE_FIELDS[i]);
				if (image == nullptr) {
					fprintf(stderr, "Entry media missing string \"%s\": %.*s\n",
						NEWSLETTER_IMAGE_FIELDS[i], (int)uri.size, uri.data);
					res.status = HTTP_STATUS_ERROR;
					return;
				}
				templateItems.Add(NEWSLETTER_IMAGE_FIELDS[i], image->ToArray());
			}
		}

		const char* MONTH_NAMES[] = {
			"ENERO", "FEBRERO", "MARZO",
			"ABRIL", "MAYO", "JUNIO",
			"JULIO", "AGOSTO", "SEPTIEMBRE",
			"OCTUBRE", "NOVIEMBRE", "DICIEMBRE"
		};
		DynamicArray<char> dateString;
		dateString.Append(entryData.dayString[0]);
		dateString.Append(entryData.dayString[1]);
		dateString.Append(ToString(" DE "));
		dateString.Append(ToString(MONTH_NAMES[entryData.monthInt - 1]));
		if (entryData.type == EntryType::NEWSLETTER) {
			templateItems.Add("subtextRight1", dateString.ToArray());
			templateItems.Add("subtextRight2", dateString.ToArray());
			templateItems.Add("subtextRight3", dateString.ToArray());
			templateItems.Add("subtextRight4", dateString.ToArray());
		}
		else {
			templateItems.Add("subtextRight", dateString.ToArray());
		}

		templateItems.Add("description", entryData.description.ToArray());
		templateItems.Add("color", entryData.color.ToArray());
		templateItems.Add("title", entryData.title.ToArray());

		auto AuthorStringConvert = [&uri](const Array<char>& author, FixedArray<char, 64>* outString) {
			outString->Clear();
			outString->Append(ToString("POR "));
			DynamicArray<char> authorUpper;
			if (!Utf8ToUppercase(author, &authorUpper)) {
				return false;
			}
			outString->Append(authorUpper.ToArray());
			return true;
		};
		FixedArray<char, 64> authorString;

		if (entryData.type == EntryType::NEWSLETTER) {
			templateItems.Add("customTop", entryData.newsletterData.customTop.ToArray());
			templateItems.Add("title1", entryData.newsletterData.titles[0].ToArray());
			templateItems.Add("title2", entryData.newsletterData.titles[1].ToArray());
			templateItems.Add("title3", entryData.newsletterData.titles[2].ToArray());
			templateItems.Add("title4", entryData.newsletterData.titles[3].ToArray());
			// TODO clean this up... also, this is storing Array (by ref),
			// so the string buffer will be overwritten and only 1 author stored
			if (!AuthorStringConvert(entryData.newsletterData.authors[0].ToArray(), &authorString)) {
				res.status = HTTP_STATUS_ERROR;
				return;
			}
			templateItems.Add("subtextLeft1", authorString.ToArray());
			if (!AuthorStringConvert(entryData.newsletterData.authors[1].ToArray(), &authorString)) {
				res.status = HTTP_STATUS_ERROR;
				return;
			}
			templateItems.Add("subtextLeft2", authorString.ToArray());
			if (!AuthorStringConvert(entryData.newsletterData.authors[2].ToArray(), &authorString)) {
				res.status = HTTP_STATUS_ERROR;
				return;
			}
			templateItems.Add("subtextLeft3", authorString.ToArray());
			if (!AuthorStringConvert(entryData.newsletterData.authors[3].ToArray(), &authorString)) {
				res.status = HTTP_STATUS_ERROR;
				return;
			}
			templateItems.Add("subtextLeft4", authorString.ToArray());
			templateItems.Add("text1", entryData.newsletterData.texts[0].ToArray());
			templateItems.Add("text2", entryData.newsletterData.texts[1].ToArray());
			templateItems.Add("text3", entryData.newsletterData.texts[2].ToArray());
			templateItems.Add("text4", entryData.newsletterData.texts[3].ToArray());
		}
		else {
			templateItems.Add("subtitle", entryData.subtitle.ToArray());
			if (!AuthorStringConvert(entryData.author.ToArray(), &authorString)) {
				res.status = HTTP_STATUS_ERROR;
				return;
			}
			templateItems.Add("subtextLeft", authorString.ToArray());
			templateItems.Add("text", entryData.text.ToArray());
		}

		if (entryData.type == EntryType::VIDEO) {
			templateItems.Add("videoID", entryData.videoID.ToArray());
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
						(int)mediaType.size, mediaType.data, (int)uri.size, uri.data);
					res.status = HTTP_STATUS_ERROR;
					return;
				}

				HashTable<Array<char>> mediaHtmlItems;
				if (StringEquals(mediaType, ToString("image"))
				|| StringEquals(mediaType, ToString("imageHalfWidth"))) {
					// TODO if there are non-image media things in the future, this will need to be
					// expanded upon / keyword tag needs to be checked for type=image
					const auto* imageLocation = GetKmkvItemStrValue(entryData.media, mediaName);
					if (imageLocation == nullptr) {
						fprintf(stderr, "Image not found: %.*s in %.*s\n",
							(int)mediaName.size, mediaName.data, (int)uri.size, uri.data);
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
						(int)mediaType.size, mediaType.data, (int)uri.size, uri.data);
					res.status = HTTP_STATUS_ERROR;
					return;
				}
				continue;
			}

			outStringMedia.Append(outString[i++]);
		}

		res.set_content(outStringMedia.data, outStringMedia.size, "text/html");
	});

	FixedArray<char, PATH_MAX_LENGTH> publicPath = rootPath;
	publicPath.Append(ToString("data/public"));
	publicPath.Append('\0');
	if (!server.set_base_dir(publicPath.data)) {
		fprintf(stderr, "server set_base_dir failed on dir %s\n", publicPath.data);
		return 1;
	}

#if SERVER_DEV
	serverDev.Get("/entries", [&allMetadataJson](const httplib::Request& req, httplib::Response& res) {
		res.set_content(allMetadataJson.data, allMetadataJson.size, "application/json");
	});

	serverDev.Get("/featured", [&featuredJson](const httplib::Request& req, httplib::Response& res) {
		res.set_content(featuredJson.data, featuredJson.size, "application/json");
	});

	serverDev.Get("/previewSite", [](const httplib::Request& req, httplib::Response& res) {
		DynamicArray<char> responseJson;
		responseJson.Append(ToString("{\"url\":\""));
#if SERVER_HTTPS
		responseJson.Append(ToString("https://preview.nopasanada.com"));
#else
		responseJson.Append(ToString("http://localhost:"));
		Array<char> portString = AllocPrintf(&defaultAllocator_, "%d", SERVER_PORT);
		defer(defaultAllocator_.Free(portString.data));
		responseJson.Append(portString);
#endif
		responseJson.Append('"');
		responseJson.Append('}');
		res.set_content(responseJson.data, responseJson.size, "application/json");
	});

	serverDev.Get("/content/[^/]+/.+", [rootPath, &mediaKmkv](const httplib::Request& req, httplib::Response& res) {
		Array<char> uri = ToString(req.path.c_str());
		if (uri[uri.size - 1] == '/') {
			uri.RemoveLast();
		}

		EntryData entryData;
		if (!LoadEntry(rootPath.ToArray(), uri, &entryData)) {
			fprintf(stderr, "LoadEntry failed for entry %.*s\n", (int)uri.size, uri.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		defer(FreeKmkv(entryData.kmkv));

		auto& tagsString = *(entryData.kmkv.GetValue("tags")->dynamicStringPtr);
		tagsString.Clear();
		tagsString.Append('[');
		for (uint64 i = 0; i < entryData.tags.size; i++) {
			tagsString.Append('"');
			tagsString.Append(entryData.tags[i].ToArray());
			tagsString.Append('"');
			tagsString.Append(',');
		}
		if (entryData.tags.size > 0) {
			tagsString.RemoveLast();
		}
		tagsString.Append(']');
		entryData.kmkv.GetValue("tags")->keywordTag.Append(ToString("array"));

		DynamicArray<char> entryJson;
		if (!KmkvToJson(entryData.kmkv, &entryJson)) {
			fprintf(stderr, "KmkvToJson failed for entry %.*s\n", (int)uri.size, uri.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}

		res.set_content(entryJson.data, entryJson.size, "application/json");
	});

	serverDev.Post("/featured", [rootPath, &featuredJson](const httplib::Request& req, httplib::Response& res) {
		Array<char> jsonString = ToString(req.body.c_str());
		HashTable<KmkvItem<StandardAllocator>> kmkv;
		if (!JsonToKmkv(jsonString, &defaultAllocator_, &kmkv)) {
			fprintf(stderr, "JsonToKmkv failed for featured json string %.*s\n",
				(int)jsonString.size, jsonString.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}

		DynamicArray<char> kmkvString;
		if (!KmkvToString(kmkv, &kmkvString)) {
			fprintf(stderr, "KmkvToString failed for featured kmkv\n");
			res.status = HTTP_STATUS_ERROR;
			return;
		}

		FixedArray<char, PATH_MAX_LENGTH> featuredKmkvPath = rootPath;
		featuredKmkvPath.Append(ToString("data/featured.kmkv"));
		const Array<uint8> kmkvData = { .size = kmkvString.size, .data = (uint8*)kmkvString.data };
		if (!WriteFile(featuredKmkvPath.ToArray(), kmkvData, false)) {
			fprintf(stderr, "WriteFile failed for featured kmkv string\n");
			res.status = HTTP_STATUS_ERROR;
			return;
		}

		if (!LoadFeaturedJson(rootPath.ToArray(), &featuredJson)) {
			fprintf(stderr, "Failed to reload featured JSON string\n");
			res.status = HTTP_STATUS_ERROR;
			return;
		}
	});

	serverDev.Post("/content/[^/]+/.+", [rootPath, &featuredJson](const httplib::Request& req, httplib::Response& res) {
		// TODO implement
	});

	serverDev.Post("/newEntry", [rootPath, &featuredJson](const httplib::Request& req, httplib::Response& res) {
		// TODO implement
	});

	serverDev.Post("/deleteEntry", [rootPath, &featuredJson](const httplib::Request& req, httplib::Response& res) {
		// TODO implement
	});

	serverDev.Post("/newImage", [](const httplib::Request& req, httplib::Response& res) {
		if (!req.has_file("imageFile")) {
			fprintf(stderr, "newImage request missing \"imageFile\"\n");
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		if (!req.has_file("npnEntryPath")) {
			fprintf(stderr, "newImage request missing \"npnEntryPath\"\n");
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		if (!req.has_file("npnLabel")) {
			fprintf(stderr, "newImage request missing \"npnLabel\"\n");
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		const auto& file = req.get_file_value("imageFile");
		const auto& npnEntryPath = req.get_file_value("npnEntryPath");
		const auto& npnLabel = req.get_file_value("npnLabel");

		// TODO implement
	});

	serverDev.Post("/reset", [rootPath, &featuredJson](const httplib::Request& req, httplib::Response& res) {
		// TODO implement
	});

	serverDev.Post("/commit", [rootPath, &featuredJson](const httplib::Request& req, httplib::Response& res) {
		// TODO implement
	});

	serverDev.Post("/deploy", [rootPath, &featuredJson](const httplib::Request& req, httplib::Response& res) {
		// TODO implement
	});

	publicPath = rootPath;
	publicPath.Append(ToString("data/public-dev"));
	publicPath.Append('\0');
	if (!serverDev.set_base_dir(publicPath.data)) {
		fprintf(stderr, "serverDev set_base_dir failed on dir %s\n", publicPath.data);
		return 1;
	}

	std::thread devThread(ServerListen, std::ref(serverDev), "localhost", SERVER_PORT_DEV);
#endif

	ServerListen(server, "localhost", SERVER_PORT);

#if SERVER_DEV
	devThread.join();
#endif

	return 0;
}

#include <cJSON.c>
#define STB_SPRINTF_IMPLEMENTATION
#include <stb_sprintf.h>
#undef STB_SPRINTF_IMPLEMENTATION
#define UTF8PROC_STATIC
#include <utf8proc.c>

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

#include <km_common/km_kmkv.cpp>
#include <km_common/km_lib.cpp>
#include <km_common/km_memory.cpp>
#include <km_common/km_os.cpp>
#include <km_common/km_string.cpp>