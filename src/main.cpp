#include <cassert>
#include <ctime>
#include <filesystem>
#if SERVER_HTTPS
#define CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_ZLIB_SUPPORT
#endif
#include <httplib.h>
#include <stb_sprintf.h>
#include <stdio.h>
#include <thread>
#include <xxhash.h>

#include <km_common/km_defines.h>
#include <km_common/km_kmkv.h>
#include <km_common/km_lib.h>
#include <km_common/km_os.h>
#include <km_common/km_string.h>

#include "settings.h"

// Homebrew. Maybe it should be standard/expected that KM apps will define these.
#define LOG_INFO(format, ...)  fprintf(stdout, format, ##__VA_ARGS__); fflush(stdout)
#define LOG_WARN(format, ...)  fprintf(stderr, format, ##__VA_ARGS__); fflush(stderr)
#define LOG_ERROR(format, ...) fprintf(stderr, format, ##__VA_ARGS__); fflush(stderr)

global_var const int HTTP_STATUS_ERROR = 500;

global_var const char* LOGIN_SESSION_COOKIE = "npn_session=";

// global_var const Array<char> IMAGE_BASE_URL = ToString("https://nopasanada.s3.amazonaws.com");
global_var const Array<char> IMAGE_BASE_URL = ToString("../../..");

#if SERVER_HTTPS
typedef httplib::SSLServer ServerType;
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

struct EntryDate
{
	char yearString[4];
	char monthString[2];
	char dayString[2];
	int yearInt;
	int monthInt;
	int dayInt;
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
	EntryDate date;
	DynamicArray<char> title;
	DynamicArray<char> description;
	DynamicArray<char> color;
	DynamicArray<char> subtitle;
	DynamicArray<char> author;
	DynamicArray<char> subtextLeft;
	DynamicArray<char> subtextRight;
	DynamicArray<char> text;

	DynamicArray<char> videoID;

	NewsletterData newsletterData;

	EntryData() {}
	~EntryData() {}
};

EntryDate GetCurrentDate()
{
	time_t t = time(NULL);
	const tm* localTime = localtime(&t);
	assert(localTime != nullptr);

	EntryDate date;
	date.yearInt = localTime->tm_year + 1900;
	date.monthInt = localTime->tm_mon + 1;
	date.dayInt = localTime->tm_mday;

	char buffer[5];
	assert(stbsp_snprintf(buffer, 5, "%04d", date.yearInt) == 4);
	MemCopy(date.yearString, buffer, 4);
	assert(stbsp_snprintf(buffer, 5, "%02d", date.monthInt) == 2);
	MemCopy(date.monthString, buffer, 2);
	assert(stbsp_snprintf(buffer, 5, "%02d", date.dayInt) == 2);
	MemCopy(date.dayString, buffer, 2);

	return date;
}

void UriToKmkvPath(const Array<char>& rootPath, const Array<char>& uri,
	FixedArray<char, PATH_MAX_LENGTH>* outPath)
{
	outPath->Clear();
	outPath->Append(rootPath);
	outPath->Append(ToString("data"));
	outPath->Append(uri);
	outPath->Append(ToString(".kmkv"));
}

bool LoadEntry(const Array<char>& rootPath, const Array<char>& uri, EntryData* outEntryData)
{
	FixedArray<char, PATH_MAX_LENGTH> kmkvPath;
	UriToKmkvPath(rootPath, uri, &kmkvPath);
	if (!LoadKmkv(kmkvPath.ToArray(), &defaultAllocator_, &outEntryData->kmkv)) {
		LOG_ERROR("LoadKmkv failed for entry %.*s\n", (int)kmkvPath.size, kmkvPath.data);
		return false;
	}

	// Load featured info
	const auto* featuredKmkv = GetKmkvItemObjValue(outEntryData->kmkv, "featured");
	if (featuredKmkv == nullptr) {
		LOG_ERROR("Entry missing \"featured\": %.*s\n", (int)kmkvPath.size, kmkvPath.data);
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
		LOG_ERROR("Entry missing \"media\": %.*s\n", (int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	outEntryData->media = *mediaKmkv;
	const auto* headerImage = GetKmkvItemStrValue(*mediaKmkv, "header");
	if (headerImage == nullptr) {
		LOG_ERROR("Entry media missing \"header\": %.*s\n", (int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	outEntryData->header = *headerImage;

	// Load entry type string and enum
	const auto* type = GetKmkvItemStrValue(outEntryData->kmkv, "type");
	if (type == nullptr) {
		LOG_ERROR("Entry missing \"type\": %.*s\n", (int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	outEntryData->type = EntryType::LAST;
	for (uint64 i = 0; i < (uint64)EntryType::LAST; i++) {
		if (StringEquals(type->ToArray(), ToString(ENTRY_TYPE_STRINGS[i]))) {
			outEntryData->type = (EntryType)i;
		}
	}
	if (outEntryData->type == EntryType::LAST) {
		LOG_ERROR("Entry with invalid \"type\": %.*s\n", (int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	outEntryData->typeString = *type;

	// Load tags string array
	const auto* tags = GetKmkvItemStrValue(outEntryData->kmkv, "tags");
	if (tags == nullptr) {
		LOG_ERROR("Entry missing \"tags\": %.*s\n", (int)kmkvPath.size, kmkvPath.data);
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
		LOG_ERROR("Entry missing string day, month, or year field(s): %.*s\n",
			(int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	if (day->size == 1) {
		outEntryData->date.dayString[0] = '0';
		outEntryData->date.dayString[1] = (*day)[0];
	}
	else if (day->size == 2) {
		outEntryData->date.dayString[0] = (*day)[0];
		outEntryData->date.dayString[1] = (*day)[1];
	}
	else {
		LOG_ERROR("Entry bad day string length %d: %.*s\n", (int)day->size,
			(int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	if (!StringToIntBase10(day->ToArray(), &outEntryData->date.dayInt)) {
		LOG_ERROR("Entry day to-integer conversion failed: %.*s\n",
			(int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	if (outEntryData->date.dayInt < 1 || outEntryData->date.dayInt > 31) {
		LOG_ERROR("Entry day %d out of range: %.*s\n", outEntryData->date.dayInt,
			(int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	if (month->size == 1) {
		outEntryData->date.monthString[0] = '0';
		outEntryData->date.monthString[1] = (*month)[0];
	}
	else if (month->size == 2) {
		outEntryData->date.monthString[0] = (*month)[0];
		outEntryData->date.monthString[1] = (*month)[1];
	}
	else {
		LOG_ERROR("Entry bad month string length %d: %.*s\n", (int)month->size,
			(int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	if (!StringToIntBase10(month->ToArray(), &outEntryData->date.monthInt)) {
		LOG_ERROR("Entry month to-integer conversion failed: %.*s\n",
			(int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	if (outEntryData->date.monthInt < 1 || outEntryData->date.monthInt > 12) {
		LOG_ERROR("Entry month %d out of range: %.*s\n", outEntryData->date.monthInt,
			(int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	if (year->size == 4) {
		outEntryData->date.yearString[0] = (*year)[0];
		outEntryData->date.yearString[1] = (*year)[1];
		outEntryData->date.yearString[2] = (*year)[2];
		outEntryData->date.yearString[3] = (*year)[3];
	}
	else {
		LOG_ERROR("Entry bad year string length %d: %.*s\n", (int)year->size,
			(int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	if (!StringToIntBase10(year->ToArray(), &outEntryData->date.yearInt)) {
		LOG_ERROR("Entry month to-integer conversion failed: %.*s\n",
			(int)kmkvPath.size, kmkvPath.data);
		return false;
	}

	// Load article contents
	const auto* title = GetKmkvItemStrValue(outEntryData->kmkv, "title");
	if (title == nullptr) {
		LOG_ERROR("Entry missing \"title\": %.*s\n", (int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	outEntryData->title = *title;
	const auto* color = GetKmkvItemStrValue(outEntryData->kmkv, "color");
	if (color == nullptr) {
		LOG_ERROR("Entry missing \"color\": %.*s\n", (int)kmkvPath.size, kmkvPath.data);
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
	const auto* subtextLeft = GetKmkvItemStrValue(outEntryData->kmkv, "subtextLeft");
	if (subtextLeft != nullptr) {
		outEntryData->subtextLeft = *subtextLeft;
	}
	const auto* subtextRight = GetKmkvItemStrValue(outEntryData->kmkv, "subtextRight");
	if (subtextRight != nullptr) {
		outEntryData->subtextRight = *subtextRight;
	}
	const auto* text = GetKmkvItemStrValue(outEntryData->kmkv, "text");
	if (text == nullptr && outEntryData->type != EntryType::NEWSLETTER) {
		LOG_ERROR("Entry missing \"text\": %.*s\n", (int)kmkvPath.size, kmkvPath.data);
		return false;
	}
	else if (text != nullptr) {
		outEntryData->text = *text;
	}

	// Load video ID
	const auto* videoID = GetKmkvItemStrValue(outEntryData->kmkv, "videoID");
	if (videoID == nullptr && outEntryData->type == EntryType::VIDEO) {
		LOG_ERROR("Entry missing \"videoID\": %.*s\n", (int)kmkvPath.size, kmkvPath.data);
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
				LOG_ERROR("Newsletter missing \"%s\": %.*s\n", TITLE_FIELD_NAMES[i],
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
				LOG_ERROR("Newsletter missing \"%s\": %.*s\n", TEXT_FIELD_NAMES[i],
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
						LOG_ERROR("Failed to find replace key %.*s\n",
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
	item->type = KmkvItemType::STRING;
	item->dynamicStringPtr = defaultAllocator_.template New<DynamicArray<char, StandardAllocator>>();
	new (item->dynamicStringPtr) DynamicArray<char, StandardAllocator>(string);
}

int CompareMetadataDateDescending(const void* p1, const void* p2)
{
	const auto* kmkv1 = *((const HashTable<KmkvItem<StandardAllocator>>**)p1);
	const auto* kmkv2 = *((const HashTable<KmkvItem<StandardAllocator>>**)p2);

	const auto* dateString1 = GetKmkvItemStrValue(*kmkv1, "dateString");
	assert(dateString1 != nullptr);
	const auto* dateString2 = GetKmkvItemStrValue(*kmkv2, "dateString");
	assert(dateString2 != nullptr);
	return StringCompare(dateString1->ToArray(), dateString2->ToArray()) * -1;
}

bool LoadAllMetadataJson(const Array<char>& rootPath, DynamicArray<char, StandardAllocator>* outJson)
{
	outJson->Clear();
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
			LOG_ERROR("Couldn't find \"content\" substring in path\n");
			return false;
		}
		Array<char> uri = pathBuffer.ToArray().Slice(start - 1, pathBuffer.size - 5);
		EntryData entryData;
		if (!LoadEntry(rootPath, uri, &entryData)) {
			LOG_ERROR("LoadEntry failed for entry %.*s\n", (int)uri.size, uri.data);
			return false;
		}

		HashTable<KmkvItem<StandardAllocator>>& metadataKmkv = *metadataKmkvs.Append();
		AllocAndSetString(metadataKmkv.Add("uri"), uri);
		AllocAndSetString(metadataKmkv.Add("type"), entryData.typeString.ToArray());
		AllocAndSetString(metadataKmkv.Add("tags"), Array<char>::empty);
		metadataKmkv.GetValue("tags")->keywordTag.Append(ToString("array"));
		auto& tagsString = *GetKmkvItemStrValue(metadataKmkv, "tags");
		for (uint64 i = 0; i < entryData.tags.size; i++) {
			tagsString.Append(entryData.tags[i].ToArray());
			tagsString.Append(',');
		}
		if (entryData.tags.size > 0) {
			tagsString.RemoveLast();
		}
		AllocAndSetString(metadataKmkv.Add("title"), entryData.title.ToArray());
		DynamicArray<char> dateString;
		dateString.Append(entryData.date.yearString[0]);
		dateString.Append(entryData.date.yearString[1]);
		dateString.Append(entryData.date.yearString[2]);
		dateString.Append(entryData.date.yearString[3]);
		dateString.Append(entryData.date.monthString[0]);
		dateString.Append(entryData.date.monthString[1]);
		dateString.Append(entryData.date.dayString[0]);
		dateString.Append(entryData.date.dayString[1]);
		AllocAndSetString(metadataKmkv.Add("dateString"), dateString.ToArray());
		AllocAndSetString(metadataKmkv.Add("author"), entryData.author.ToArray());
		AllocAndSetString(metadataKmkv.Add("subtitle"), entryData.subtitle.ToArray());
		AllocAndSetString(metadataKmkv.Add("image"), entryData.header.ToArray());

		auto* featured = metadataKmkv.Add("featuredInfo");
		featured->type = KmkvItemType::KMKV;
		featured->hashTablePtr = defaultAllocator_.template New<HashTable<KmkvItem<StandardAllocator>>>();
		new (featured->hashTablePtr) HashTable<KmkvItem<StandardAllocator>>();
		auto& featuredKmkv = *featured->hashTablePtr;
		AllocAndSetString(featuredKmkv.Add("pretitle"), entryData.featuredPretitle.ToArray());
		AllocAndSetString(featuredKmkv.Add("title"), entryData.featuredTitle.ToArray());
		AllocAndSetString(featuredKmkv.Add("text1"), entryData.featuredText1.ToArray());
		AllocAndSetString(featuredKmkv.Add("text2"), entryData.featuredText2.ToArray());
		AllocAndSetString(featuredKmkv.Add("highlightColor"), entryData.featuredHighlightColor.ToArray());

		// TODO look up "featured1" ... "featuredN" in entry media and use that if present
		AllocAndSetString(featuredKmkv.Add("images"), Array<char>::empty);
		featuredKmkv.GetValue("images")->keywordTag.Append(ToString("array"));
		auto& featuredImagesString = *GetKmkvItemStrValue(featuredKmkv, "images");
		featuredImagesString.Append(entryData.header.ToArray());
	}

	DynamicArray<HashTable<KmkvItem<StandardAllocator>>*> metadataKmkvPtrs(metadataKmkvs.size);
	for (uint64 i = 0; i < metadataKmkvs.size; i++) {
		metadataKmkvPtrs.Append(&metadataKmkvs[i]);
	}

	qsort(metadataKmkvPtrs.data, metadataKmkvPtrs.size,
		sizeof(HashTable<KmkvItem<StandardAllocator>>*), CompareMetadataDateDescending);

	for (uint64 i = 0; i < metadataKmkvPtrs.size; i++) {
		if (!KmkvToJson(*metadataKmkvPtrs[i], outJson)) {
			LOG_ERROR("KmkvToJson failed for entry %.*s\n", (int)pathBuffer.size, pathBuffer.data);
			return false;
		}
		outJson->Append(',');
	}

	outJson->RemoveLast();
	outJson->Append(']');
	return true;
}

bool LoadCategoriesJson(const Array<char>& rootPath, DynamicArray<char, StandardAllocator>* outJson)
{
	FixedArray<char, PATH_MAX_LENGTH> categoriesKmkvPath;
	categoriesKmkvPath.Clear();
	categoriesKmkvPath.Append(rootPath);
	categoriesKmkvPath.Append(ToString("data/categories.kmkv"));

	HashTable<KmkvItem<StandardAllocator>> categoriesKmkv;
	if (!LoadKmkv(categoriesKmkvPath.ToArray(), &defaultAllocator_, &categoriesKmkv)) {
		LOG_ERROR("LoadKmkv failed for categories: %.*s\n",
			(int)categoriesKmkvPath.size, categoriesKmkvPath.data);
		return false;
	}

	// TODO double check that displayOrder references valid categories here

	outJson->Clear();
	if (!KmkvToJson(categoriesKmkv, outJson)) {
		LOG_ERROR("KmkvToJson failed for categories: %.*s\n",
			(int)categoriesKmkvPath.size, categoriesKmkvPath.data);
		return false;
	}

	return true;
}

bool ServerListen(ServerType& server, const char* host, int port)
{
	LOG_INFO("Listening on host \"%s\", port %d\n", host, port);
	if (!server.listen(host, port)) {
		LOG_ERROR("server listen failed for host \"%s\", port %d\n", host, port);
		return false;
	}

	return true;
}

void GenerateSessionId(const Array<char>& username, const Array<char>& password,
	DynamicArray<char, StandardAllocator>* outSessionId)
{
	outSessionId->Clear();
	outSessionId->Append(username);
	outSessionId->Append(password);
	XXH64_hash_t hash = XXH64(outSessionId->data, outSessionId->size, (XXH64_hash_t)time(NULL));
	char buffer[17];
	assert(stbsp_snprintf(buffer, 17, "%016I64x", hash) == 16);
	outSessionId->Clear();
	outSessionId->Append(ToString(buffer));
}

bool IsLoginValid(const Array<char>& username, const Array<char>& password,
	const HashTable<KmkvItem<StandardAllocator>>& loginsKmkv)
{
	const auto* userPassword = GetKmkvItemStrValue(loginsKmkv, username);
	if (userPassword == nullptr) {
		return false;
	}

	return StringEquals(userPassword->ToArray(), password);
}

bool IsAuthenticated(const httplib::Request& req, const DynamicArray<DynamicArray<char>>& sessions)
{
	if (!req.has_header("Cookie")) {
		return false;
	}
	std::string cookieStdString = req.get_header_value("Cookie");
	Array<char> cookieString = ToString(cookieStdString);
	uint64 sessionInd = SubstringSearch(cookieString, ToString(LOGIN_SESSION_COOKIE));
	if (sessionInd == cookieString.size) {
		return false;
	}
	uint64 sessionStart = cookieString.FindFirst('=', sessionInd);
	uint64 sessionEnd = cookieString.FindFirst(';', sessionInd);
	Array<char> session = cookieString.Slice(sessionStart + 1, sessionEnd);
	for (uint64 i = 0; i < sessions.size; i++) {
		if (StringEquals(session, sessions[i].ToArray())) {
			return true;
		}
	}
	return false;
}

#define CHECK_AUTH_OR_ERROR(request, response, sessions) if (!IsAuthenticated((request), (sessions))) { \
	(response).status = HTTP_STATUS_ERROR; \
	return; }

int main(int argc, char** argv)
{
#if SERVER_HTTPS
	ServerType server(SERVER_CERT, SERVER_KEY);
#else
	ServerType server;
#endif

#if 0
	// Test S3 object put
#if SERVER_HTTPS
	httplib::SSLClient client("nopasanada.s3.amazonaws.com");
#else
	httplib::Client client("nopasanada.s3.amazonaws.com");
#endif

	httplib::Headers headers;
	headers.insert(std::pair("hello", "goodbye"));
	std::shared_ptr<httplib::Response> response = client.Put("/images/202001/headers/i-am-test.jpg",
		headers, "", "image/jpeg");
	if (!response) {
		LOG_ERROR("Amazon S3 PUT request failed\n");
		return 1;
	}

	LOG_INFO("Response status %d, body:\n%s\n", response->status, response->body.c_str());

	return 0;
#endif

	FixedArray<char, PATH_MAX_LENGTH> rootPath = GetExecutablePath(&defaultAllocator_);
	if (rootPath.size == 0) {
		LOG_ERROR("Failed to get executable path\n");
		return 1;
	}
	rootPath.RemoveLast();
	rootPath.size = rootPath.ToArray().FindLast('/') + 1;
	LOG_INFO("Root path: %.*s\n", (int)rootPath.size, rootPath.data);

	FixedArray<char, PATH_MAX_LENGTH> mediaKmkvPath = rootPath;
	mediaKmkvPath.Append(ToString("data/media.kmkv"));
	HashTable<KmkvItem<StandardAllocator>> mediaKmkv;
	if (!LoadKmkv(mediaKmkvPath.ToArray(), &defaultAllocator_, &mediaKmkv)) {
		LOG_ERROR("LoadKmkv failed for media file\n");
		return 1;
	}

	DynamicArray<char> allMetadataJson;
	if (!LoadAllMetadataJson(rootPath.ToArray(), &allMetadataJson)) {
		LOG_ERROR("Failed to load all entry metadata to JSON\n");
		return 1;
	}
	// LOG_INFO("Metadata JSON:\n%.*s\n", (int)allMetadataJson.size, allMetadataJson.data);

	DynamicArray<char> categoriesJson;
	if (!LoadCategoriesJson(rootPath.ToArray(), &categoriesJson)) {
		LOG_ERROR("Failed to load categories to JSON\n");
		return 1;
	}

	// Backwards compatibility =====================================================================
	server.Get("/el-caso-diet-prada", [](const auto& req, auto& res) {
		res.set_redirect("/content/201908/el-caso-diet-prada");
	});
	server.Get("/la-cerveza-si-es-cosa-de-mujeres", [](const auto& req, auto& res) {
		res.set_redirect("/content/201908/la-cerveza-si-es-cosa-de-mujeres");
	});
	server.Get("/content/201908/el-amazonas", [](const auto& req, auto& res) {
		res.set_redirect("/content/201908/newsletter-29");
	});
	server.Get("/content/201909/newsletter-03", [](const auto& req, auto& res) {
		res.set_redirect("/content/201909/newsletter-03");
	});
	server.Get("/tailor-to-suit", [](const auto& req, auto& res) {
		res.set_redirect("/content/201909/tailor-to-suit");
	});
	// =============================================================================================

	server.Get("/entries", [&allMetadataJson](const auto& req, auto& res) {
		res.set_content(allMetadataJson.data, allMetadataJson.size, "application/json");
	});

	server.Get("/categories", [&categoriesJson](const auto& req, auto& res) {
		res.set_content(categoriesJson.data, categoriesJson.size, "application/json");
	});

	server.Get("/content/[^/]+/.+", [&rootPath, &mediaKmkv](const auto& req, auto& res) {
		Array<char> uri = ToString(req.path);
		if (uri[uri.size - 1] == '/') {
			uri.RemoveLast();
		}

		EntryData entryData;
		if (!LoadEntry(rootPath.ToArray(), uri, &entryData)) {
			LOG_ERROR("LoadEntry failed for entry %.*s\n", (int)uri.size, uri.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}

		FixedArray<char, PATH_MAX_LENGTH> templatePath = rootPath;
		templatePath.Append(ToString("data/templates/"));
		templatePath.Append(entryData.typeString.ToArray());
		templatePath.Append(ToString(".html"));
		templatePath.Append('\0');
		Array<uint8> templateFile = LoadEntireFile(templatePath.ToArray(), &defaultAllocator_);
		if (templateFile.data == nullptr) {
			LOG_ERROR("Failed to load template file at %.*s\n",
				(int)templatePath.size, templatePath.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		defer(FreeFile(templateFile, &defaultAllocator_));

		HashTable<Array<char>> templateItems;
		templateItems.Add("uri", uri);
		templateItems.Add("imageBaseUrl", IMAGE_BASE_URL);
		templateItems.Add("image", entryData.header.ToArray());

		if (entryData.type == EntryType::NEWSLETTER) {
			const char* NEWSLETTER_IMAGE_FIELDS[] = {
				"header-desktop1", "header-desktop2", "header-desktop3", "header-desktop4",
				"header-mobile1",  "header-mobile2",  "header-mobile3",  "header-mobile4"
			};
			for (int i = 0; i < 8; i++) {
				const auto* image = GetKmkvItemStrValue(entryData.media, NEWSLETTER_IMAGE_FIELDS[i]);
				if (image == nullptr) {
					LOG_ERROR("Entry media missing string \"%s\": %.*s\n",
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
		dateString.Append(entryData.date.dayString[0]);
		dateString.Append(entryData.date.dayString[1]);
		dateString.Append(ToString(" DE "));
		dateString.Append(ToString(MONTH_NAMES[entryData.date.monthInt - 1]));
		if (entryData.type == EntryType::NEWSLETTER) {
			templateItems.Add("subtextRight1", dateString.ToArray());
			templateItems.Add("subtextRight2", dateString.ToArray());
			templateItems.Add("subtextRight3", dateString.ToArray());
			templateItems.Add("subtextRight4", dateString.ToArray());
		}
		else if (entryData.type == EntryType::TEXT) {
			templateItems.Add("subtextRight", entryData.subtextRight.ToArray());
		}
		else {
			templateItems.Add("subtextRight", dateString.ToArray());
		}

		templateItems.Add("description", entryData.description.ToArray());
		templateItems.Add("color", entryData.color.ToArray());
		templateItems.Add("title", entryData.title.ToArray());

		auto AuthorStringConvert = [&uri](const Array<char>& author, FixedArray<char, 64>* outString) {
			outString->Clear();
			if (author.size == 0) {
				return true;
			}
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
			if (entryData.type == EntryType::TEXT) {
				templateItems.Add("subtextLeft", entryData.subtextLeft.ToArray());
			}
			else {
				templateItems.Add("subtextLeft", authorString.ToArray());
			}
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
			LOG_ERROR("Failed to search-and-replace to template file %.*s\n",
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
					LOG_ERROR("Media type not found: %.*s in %.*s\n",
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
					if (imageLocation != nullptr) {
						mediaHtmlItems.Add("location", imageLocation->ToArray());
					}
					else {
						LOG_ERROR("Image not found: %.*s in %.*s\n",
							(int)mediaName.size, mediaName.data, (int)uri.size, uri.data);
						mediaHtmlItems.Add("location", ToString(""));
					}
				}
				else {
					mediaHtmlItems.Add("location", mediaName);
				}
				// TODO implement style extraction from KMKV (multiple keyword tag support)
				mediaHtmlItems.Add("style", Array<char>::empty);
				if (!SearchReplaceAndAppend(mediaHtml->ToArray(), mediaHtmlItems, &outStringMedia)) {
					LOG_ERROR("Failed to search-and-replace media HTML, type %.*s in %.*s\n",
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

	FixedArray<char, PATH_MAX_LENGTH> imageRootPath = rootPath;
	imageRootPath.RemoveLast();
	uint64 lastSlash = imageRootPath.ToArray().FindLast('/');
	if (lastSlash == imageRootPath.size) {
		LOG_ERROR("Bad public path, no directory above for images: %.*s\n",
			(int)imageRootPath.size, imageRootPath.data);
		return 1;
	}
	imageRootPath.size = lastSlash + 1;
	imageRootPath.Append(ToString("nopasanada-images"));
	imageRootPath.Append('\0');
	if (!server.set_base_dir(imageRootPath.data, "/images")) {
		LOG_ERROR("server set_base_dir failed on dir %s\n", imageRootPath.data);
		return 1;
	}
	imageRootPath.RemoveLast();
	imageRootPath.Append('/');

	FixedArray<char, PATH_MAX_LENGTH> publicPath = rootPath;
	publicPath.Append(ToString("data/public"));
	publicPath.Append('\0');
	if (!server.set_base_dir(publicPath.data, "/")) {
		LOG_ERROR("server set_base_dir failed on dir %s\n", publicPath.data);
		return 1;
	}

#if SERVER_DEV
	FixedArray<char, PATH_MAX_LENGTH> loginsPath = rootPath;
	loginsPath.Append(ToString("keys/logins.kmkv"));
	HashTable<KmkvItem<StandardAllocator>> loginsKmkv;
	if (!LoadKmkv(loginsPath.ToArray(), &defaultAllocator_, &loginsKmkv)) {
		LOG_ERROR("Failed to load logins kmkv\n");
		return 1;
	}
	DynamicArray<DynamicArray<char>> sessions;

#if SERVER_HTTPS
	ServerType serverDev(SERVER_CERT, SERVER_KEY);
#else
	ServerType serverDev;
#endif

	// serverDev.set_error_handler([](const auto& req, auto& res) {
	// 	auto fmt = "<p>Error Status: <span style='color:red;'>%d</span></p>";
	// 	char buf[BUFSIZ];
	// 	snprintf(buf, sizeof(buf), fmt, res.status);
	// 	res.set_content(buf, "text/html");
	// });

	serverDev.set_file_request_handler([&sessions](const auto& req, auto& res) {
		if ((req.path == "/" || req.path == "/entry/") && !IsAuthenticated(req, sessions)) {
			res.set_redirect("/login/");
			return;
		}
	});

	serverDev.Post("/authenticate", [&loginsKmkv, &sessions](const auto& req, auto& res) {
		Array<char> bodyJson = ToString(req.body);
		uint64 indEqual = bodyJson.FindFirst('=');
		if (indEqual == bodyJson.size) {
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		if (!StringEquals(bodyJson.SliceTo(indEqual), ToString("username"))) {
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		uint64 indAmpersand = bodyJson.FindFirst('&');
		if (indAmpersand == bodyJson.size) {
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		Array<char> username = bodyJson.Slice(indEqual + 1, indAmpersand);
		indEqual = bodyJson.FindFirst('=', indEqual + 1);
		if (indEqual == bodyJson.size) {
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		if (!StringEquals(bodyJson.Slice(indAmpersand + 1, indEqual), ToString("password"))) {
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		Array<char> password = bodyJson.SliceFrom(indEqual + 1);

		if (IsLoginValid(username, password, loginsKmkv)) {
			LOG_INFO("Successful log in: %.*s\n", (int)username.size, username.data);
			DynamicArray<char>* newSession = sessions.Append();
			GenerateSessionId(username, password, newSession);
			std::string newSessionStd(newSession->data, newSession->size);
			res.set_header("Set-Cookie", std::string(LOGIN_SESSION_COOKIE) + newSessionStd);
			res.set_redirect("/");
		}
		else {
			LOG_INFO("Failed log in: %.*s\n", (int)username.size, username.data);
			res.set_redirect("/login/");
		}
	});

	serverDev.Get("/entries", [&allMetadataJson](const auto& req, auto& res) {
		res.set_content(allMetadataJson.data, allMetadataJson.size, "application/json");
	});

	serverDev.Get("/categories", [&categoriesJson](const auto& req, auto& res) {
		res.set_content(categoriesJson.data, categoriesJson.size, "application/json");
	});

	serverDev.Get("/previewSite", [](const auto& req, auto& res) {
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

	serverDev.Get("/content/[^/]+/.+", [&rootPath, &mediaKmkv](const auto& req, auto& res) {
		Array<char> uri = ToString(req.path);
		if (uri[uri.size - 1] == '/') {
			uri.RemoveLast();
		}

		EntryData entryData;
		if (!LoadEntry(rootPath.ToArray(), uri, &entryData)) {
			LOG_ERROR("LoadEntry failed for entry %.*s\n", (int)uri.size, uri.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}

		DynamicArray<char> entryJson;
		if (!KmkvToJson(entryData.kmkv, &entryJson)) {
			LOG_ERROR("KmkvToJson failed for entry %.*s\n", (int)uri.size, uri.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}

		res.set_content(entryJson.data, entryJson.size, "application/json");
	});

	serverDev.Post("/featured", [&rootPath, &categoriesJson, &sessions](const auto& req, auto& res) {
		CHECK_AUTH_OR_ERROR(req, res, sessions);

		Array<char> jsonString = ToString(req.body);
		HashTable<KmkvItem<StandardAllocator>> newFeaturedKmkv;
		if (!JsonToKmkv(jsonString, &defaultAllocator_, &newFeaturedKmkv)) {
			LOG_ERROR("JsonToKmkv failed for featured json string %.*s\n",
				(int)jsonString.size, jsonString.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}

		FixedArray<char, PATH_MAX_LENGTH> categoriesKmkvPath = rootPath;
		categoriesKmkvPath.Append(ToString("data/categories.kmkv"));
		HashTable<KmkvItem<StandardAllocator>> categoriesKmkv;
		if (!LoadKmkv(categoriesKmkvPath.ToArray(), &defaultAllocator_, &categoriesKmkv)) {
			LOG_ERROR("Failed to load categories KMKV on save\n");
			res.status = HTTP_STATUS_ERROR;
			return;
		}

		for (uint64 i = 0; i < newFeaturedKmkv.capacity; i++) {
			const HashKey& category = newFeaturedKmkv.pairs[i].key;
			if (category.string.size == 0) {
				continue;
			}
			const KmkvItem<StandardAllocator>& newCategoryInfoItem = newFeaturedKmkv.pairs[i].value;
			if (newCategoryInfoItem.type == KmkvItemType::STRING) {
				LOG_ERROR("New featured category string type, expected object: %.*s\n",
					(int)category.string.size, category.string.data);
				res.status = HTTP_STATUS_ERROR;
				return;
			}
			const auto& newCategoryInfo = *newCategoryInfoItem.hashTablePtr;
			auto* categoryInfo = GetKmkvItemObjValue(categoriesKmkv, category);
			if (categoryInfo == nullptr) {
				LOG_ERROR("Existing categories missing category %.*s\n",
					(int)category.string.size, category.string.data);
				res.status = HTTP_STATUS_ERROR;
				return;
			}

			for (uint64 j = 0; j < newCategoryInfo.capacity; j++) {
				const HashKey& key = newCategoryInfo.pairs[j].key;
				if (key.string.size == 0) {
					continue;
				}
				const KmkvItem<StandardAllocator>& item = newCategoryInfo.pairs[j].value;

				if (StringEquals(key.string.ToArray(), ToString("featured"))) {
					auto* categoryFeatured = GetKmkvItemStrValue(*categoryInfo, "featured");
					if (categoryFeatured == nullptr) {
						LOG_ERROR("Existing categories missing category \"featured\": %.*s\n",
							(int)category.string.size, category.string.data);
						res.status = HTTP_STATUS_ERROR;
						return;
					}
					categoryFeatured->Clear();
					categoryFeatured->FromArray(item.dynamicStringPtr->ToArray());
					continue;
				}

				if (item.type != KmkvItemType::KMKV) {
					LOG_ERROR("New featured subcategory incorrect type, expected object: %.*s\n",
						(int)key.string.size, key.string.data);
					res.status = HTTP_STATUS_ERROR;
					return;
				}
				const auto* newFeatured = GetKmkvItemStrValue(*item.hashTablePtr, "featured");
				if (newFeatured == nullptr) {
					LOG_ERROR("New featured subcategory has no \"featured\": %.*s\n",
						(int)key.string.size, key.string.data);
					res.status = HTTP_STATUS_ERROR;
					return;
				}
				auto* subcategoryInfo = GetKmkvItemObjValue(*categoryInfo, key);
				if (subcategoryInfo == nullptr) {
					LOG_ERROR("Existing categories missing subcategory %.*s of %.*s\n",
						(int)key.string.size, key.string.data,
						(int)category.string.size, category.string.data);
					res.status = HTTP_STATUS_ERROR;
					return;
				}
				auto* subcategoryFeatured = GetKmkvItemStrValue(*subcategoryInfo, "featured");
				if (subcategoryFeatured == nullptr) {
					LOG_ERROR("Existing categories missing subcategory \"featured\" %.*s of %.*s\n",
						(int)key.string.size, key.string.data,
						(int)category.string.size, category.string.data);
					res.status = HTTP_STATUS_ERROR;
					return;
				}
				subcategoryFeatured->Clear();
				subcategoryFeatured->FromArray(newFeatured->ToArray());
			}
		}

		DynamicArray<char> kmkvString;
		if (!KmkvToString(categoriesKmkv, &kmkvString)) {
			LOG_ERROR("KmkvToString failed for categories kmkv\n");
			res.status = HTTP_STATUS_ERROR;
			return;
		}

		const Array<uint8> kmkvData = { .size = kmkvString.size, .data = (uint8*)kmkvString.data };
		if (!WriteFile(categoriesKmkvPath.ToArray(), kmkvData, false)) {
			LOG_ERROR("WriteFile failed for categories kmkv string\n");
			res.status = HTTP_STATUS_ERROR;
			return;
		}

		if (!LoadCategoriesJson(rootPath.ToArray(), &categoriesJson)) {
			LOG_ERROR("Failed to reload categories JSON string\n");
			res.status = HTTP_STATUS_ERROR;
			return;
		}
	});

	serverDev.Post("/content/[^/]+/.+", [&rootPath, &allMetadataJson, &sessions](const auto& req, auto& res) {
		CHECK_AUTH_OR_ERROR(req, res, sessions);

		Array<char> uri = ToString(req.path);
		if (uri[uri.size - 1] == '/') {
			uri.RemoveLast();
		}

		EntryData entryData;
		if (!LoadEntry(rootPath.ToArray(), uri, &entryData)) {
			LOG_ERROR("LoadEntry failed for entry %.*s\n", (int)uri.size, uri.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}

		Array<char> entryJson = ToString(req.body);
		HashTable<KmkvItem<StandardAllocator>> kmkv;
		if (!JsonToKmkv(entryJson, &defaultAllocator_, &kmkv)) {
			LOG_ERROR("JsonToKmkv failed for entry %.*s\n", (int)uri.size, uri.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}

		// TODO cross-reference entryData.kmkv with received kmkv, modify entryData.kmkv accordingly
		// for now, we're just YOLO-writing what we get.
		DynamicArray<char> kmkvString;
		if (!KmkvToString(kmkv, &kmkvString)) {
			LOG_ERROR("KmkvToString failed for entry %.*s\n", (int)uri.size, uri.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		FixedArray<char, PATH_MAX_LENGTH> kmkvPath;
		UriToKmkvPath(rootPath.ToArray(), uri, &kmkvPath);
		Array<uint8> newData = { .size = kmkvString.size, .data = (uint8*)kmkvString.data };
		if (!WriteFile(kmkvPath.ToArray(), newData, false)) {
			LOG_ERROR("Failed to write kmkv data to file for entry %.*s\n",
				(int)uri.size, uri.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		LOG_INFO("Saved entry %.*s\n", (int)uri.size, uri.data);

		if (!LoadAllMetadataJson(rootPath.ToArray(), &allMetadataJson)) {
			LOG_ERROR("Failed to reload all entry metadata to JSON for entry %.*s\n",
				(int)uri.size, uri.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
	});

	serverDev.Post("/newEntry", [&rootPath, &allMetadataJson, &sessions](const auto& req, auto& res) {
		CHECK_AUTH_OR_ERROR(req, res, sessions);

		Array<char> bodyJson = ToString(req.body);
		HashTable<KmkvItem<StandardAllocator>> kmkv;
		if (!JsonToKmkv(bodyJson, &defaultAllocator_, &kmkv)) {
			LOG_ERROR("JsonToKmkv failed for deleteEntry request body %.*s\n",
				(int)bodyJson.size, bodyJson.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}

		const auto* name = GetKmkvItemStrValue(kmkv, "uniqueName");
		const auto* type = GetKmkvItemStrValue(kmkv, "contentType");
		const auto* copyFrom = GetKmkvItemStrValue(kmkv, "copyFrom");
		if (name == nullptr || type == nullptr) {
			LOG_ERROR("newEntry request missing \"uniqueName\" or \"contentType\" fields\n");
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		for (uint64 i = 0; i < name->size; i++) {
			if (!IsAlphanumeric((*name)[i]) && (*name)[i] != '-') {
				LOG_ERROR("newEntry name contains invalid characters: %.*s\n",
					(int)name->size, name->data);
				res.status = HTTP_STATUS_ERROR;
				return;
			}
		}

		FixedArray<char, PATH_MAX_LENGTH> srcKmkvPath = rootPath;
		if (copyFrom == nullptr) {
			srcKmkvPath.Append(ToString("data/templates/"));
			srcKmkvPath.Append(type->ToArray());
			srcKmkvPath.Append(ToString(".kmkv"));
		}
		else {
			UriToKmkvPath(rootPath.ToArray(), copyFrom->ToArray(), &srcKmkvPath);
		}

		HashTable<KmkvItem<StandardAllocator>> srcKmkv;
		if (!LoadKmkv(srcKmkvPath.ToArray(), &defaultAllocator_, &srcKmkv)) {
			LOG_ERROR("Failed to load source kmkv %.*s for new entry %.*s\n",
				(int)srcKmkvPath.size, srcKmkvPath.data, (int)name->size, name->data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		EntryDate currentDate = GetCurrentDate();
		auto* day = GetKmkvItemStrValue(srcKmkv, "day");
		if (day == nullptr) {
			// TODO maybe I need some helper function, AllocAndSetString isn't the best
			AllocAndSetString(srcKmkv.Add("day"), Array<char>::empty);
			day = GetKmkvItemStrValue(srcKmkv, "day");
		}
		day->Clear();
		day->Append(currentDate.dayString[0]);
		day->Append(currentDate.dayString[1]);
		auto* month = GetKmkvItemStrValue(srcKmkv, "month");
		if (month == nullptr) {
			AllocAndSetString(srcKmkv.Add("month"), Array<char>::empty);
			month = GetKmkvItemStrValue(srcKmkv, "month");
		}
		month->Clear();
		month->Append(currentDate.monthString[0]);
		month->Append(currentDate.monthString[1]);
		auto* year = GetKmkvItemStrValue(srcKmkv, "year");
		if (year == nullptr) {
			AllocAndSetString(srcKmkv.Add("year"), Array<char>::empty);
			year = GetKmkvItemStrValue(srcKmkv, "year");
		}
		year->Clear();
		year->Append(currentDate.yearString[0]);
		year->Append(currentDate.yearString[1]);
		year->Append(currentDate.yearString[2]);
		year->Append(currentDate.yearString[3]);

		DynamicArray<char> srcKmkvString;
		if (!KmkvToString(srcKmkv, &srcKmkvString)) {
			LOG_ERROR("KmkvToString failed for src entry %.*s\n",
				(int)srcKmkvPath.size, srcKmkvPath.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}

		DynamicArray<char> newKmkvPath;
		newKmkvPath.Append(rootPath.ToArray());
		newKmkvPath.Append(ToString("data/content/"));
		newKmkvPath.Append(currentDate.yearString[0]);
		newKmkvPath.Append(currentDate.yearString[1]);
		newKmkvPath.Append(currentDate.yearString[2]);
		newKmkvPath.Append(currentDate.yearString[3]);
		newKmkvPath.Append(currentDate.monthString[0]);
		newKmkvPath.Append(currentDate.monthString[1]);
		newKmkvPath.Append('/');
		if (!CreateDirRecursive(newKmkvPath.ToArray())) {
			LOG_ERROR("Failed to create directory in newEntry request: %.*s\n",
				(int)newKmkvPath.size, newKmkvPath.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		newKmkvPath.Append(name->ToArray());
		newKmkvPath.Append(ToString(".kmkv"));
		Array<uint8> newData = { .size = srcKmkvString.size, .data = (uint8*)srcKmkvString.data };
		if (!WriteFile(newKmkvPath.ToArray(), newData, false)) {
			LOG_ERROR("Failed to write new kmkv data to file %.*s\n",
				(int)newKmkvPath.size, newKmkvPath.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		LOG_INFO("Created entry %.*s\n", (int)newKmkvPath.size, newKmkvPath.data);

		if (!LoadAllMetadataJson(rootPath.ToArray(), &allMetadataJson)) {
			LOG_ERROR("Failed to reload all entry metadata to JSON after new entry %.*s\n",
				(int)newKmkvPath.size, newKmkvPath.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
	});

	serverDev.Post("/deleteEntry", [&rootPath, &allMetadataJson, &sessions](const auto& req, auto& res) {
		CHECK_AUTH_OR_ERROR(req, res, sessions);

		Array<char> bodyJson = ToString(req.body);
		HashTable<KmkvItem<StandardAllocator>> kmkv;
		if (!JsonToKmkv(bodyJson, &defaultAllocator_, &kmkv)) {
			LOG_ERROR("JsonToKmkv failed for deleteEntry request body %.*s\n",
				(int)bodyJson.size, bodyJson.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}

		const auto* uri = GetKmkvItemStrValue(kmkv, "uri");
		if (uri == nullptr) {
			LOG_ERROR("No uri in deleteEntry request\n");
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		FixedArray<char, PATH_MAX_LENGTH> kmkvPath;
		UriToKmkvPath(rootPath.ToArray(), uri->ToArray(), &kmkvPath);
		if (!DeleteFile(kmkvPath.ToArray(), true)) {
			LOG_ERROR("Failed to delete entry file %.*s\n", (int)kmkvPath.size, kmkvPath.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		LOG_INFO("Deleted entry %.*s\n", (int)kmkvPath.size, kmkvPath.data);

		if (!LoadAllMetadataJson(rootPath.ToArray(), &allMetadataJson)) {
			LOG_ERROR("Failed to reload all entry metadata to JSON after deleting %.*s\n",
				(int)uri->size, uri->data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
	});

	serverDev.Post("/newImage", [&imageRootPath, &sessions](const auto& req, auto& res) {
		CHECK_AUTH_OR_ERROR(req, res, sessions);

		if (!req.has_file("imageFile")) {
			LOG_ERROR("newImage request missing \"imageFile\"\n");
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		if (!req.has_file("npnEntryUri")) {
			LOG_ERROR("newImage request missing \"npnEntryUri\"\n");
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		if (!req.has_file("npnLabel")) {
			LOG_ERROR("newImage request missing \"npnLabel\"\n");
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		const auto& fileData = req.get_file_value("imageFile");
		const auto& uriData = req.get_file_value("npnEntryUri");
		const auto& labelData = req.get_file_value("npnLabel");

		if (fileData.content_type != "image/jpeg") {
			LOG_ERROR("non-jpg file in newImage request\n");
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		Array<char> uri = ToString(uriData.content);
		Array<char> label = ToString(labelData.content);
		for (uint64 i = 0; i < label.size; i++) {
			if (!IsAlphanumeric(label[i]) && label[i] != '-') {
				LOG_ERROR("Invalid npnLabel: %.*s\n", (int)label.size, label.data);
				res.status = HTTP_STATUS_ERROR;
				return;
			}
		}

		DynamicArray<Array<char>> uriSplit;
		StringSplit(uri, '/', &uriSplit);
		if (uriSplit.size != 4) {
			LOG_ERROR("Bad uri split (%d) in newImage request: %.*s\n", (int)uriSplit.size,
				(int)uri.size, uri.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}

		DynamicArray<char> imageUri;
		imageUri.Append(ToString("/images/"));
		imageUri.Append(uriSplit[2]);
		imageUri.Append('/');
		if (StringEquals(label, ToString("header"))) {
			imageUri.Append(ToString("headers/"));
			imageUri.Append(uriSplit[3]);
			imageUri.Append(ToString(".jpg"));
		}
		else if (StringEquals(label, ToString("poster"))) {
			imageUri.Append(ToString("posters/"));
			imageUri.Append(uriSplit[3]);
			imageUri.Append(ToString(".jpg"));
		}
		else if (StringContains(label, ToString("header-desktop"))) {
			char number = label[label.size - 1];
			if (number != '1' && number != '2' && number != '3' && number != '4') {
				LOG_ERROR("Invalid npnLabel: %.*s\n", (int)label.size, label.data);
				res.status = HTTP_STATUS_ERROR;
				return;
			}
			imageUri.Append(uriSplit[3]);
			imageUri.Append(ToString("/vertical"));
			imageUri.Append(number);
			imageUri.Append(ToString(".jpg"));
		}
		else if (StringContains(label, ToString("header-mobile"))) {
			char number = label[label.size - 1];
			if (number != '1' && number != '2' && number != '3' && number != '4') {
				LOG_ERROR("Invalid npnLabel: %.*s\n", (int)label.size, label.data);
				res.status = HTTP_STATUS_ERROR;
				return;
			}
			imageUri.Append(uriSplit[3]);
			imageUri.Append(ToString("/square"));
			imageUri.Append(number);
			imageUri.Append(ToString(".jpg"));
		}
		else {
			imageUri.Append(uriSplit[3]);
			imageUri.Append('/');
			imageUri.Append(label);
			imageUri.Append(ToString(".jpg"));
		}

		FixedArray<char, PATH_MAX_LENGTH> imagePath = imageRootPath;
		uint64 secondSlash = imageUri.ToArray().FindFirst('/', 1);
		imagePath.Append(imageUri.ToArray().SliceFrom(secondSlash + 1)); 

		Array<char> imageDir = imagePath.ToArray();
		uint64 lastSlash = imageDir.FindLast('/');
		if (lastSlash == imageDir.size) {
			LOG_ERROR("bad image file path in newImage request: %.*s\n",
				(int)imageDir.size, imageDir.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		imageDir.size = lastSlash + 1;
		if (!CreateDirRecursive(imageDir)) {
			LOG_ERROR("Failed to create image directory in newImage request: %.*s\n",
				(int)imageDir.size, imageDir.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}

		Array<uint8> fileContents = {
			.size = fileData.content.size(),
			.data = (uint8*)fileData.content.c_str()
		};
		if (!WriteFile(imagePath.ToArray(), fileContents, false)) {
			LOG_ERROR("Failed to write image file data in newImage request, path %.*s\n",
				(int)imagePath.size, imagePath.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}

		LOG_INFO("Upload successful for %s in %.*s, uri %.*s\n", fileData.filename.c_str(),
			(int)imagePath.size, imagePath.data, (int)imageUri.size, imageUri.data);
		DynamicArray<char> responseXml;
		responseXml.Append(ToString("<uri>"));
		responseXml.Append(imageUri.ToArray());
		responseXml.Append(ToString("</uri>"));
		res.set_content(responseXml.data, responseXml.size, "application/xml");
	});

	serverDev.Post("/reset", [&rootPath, &sessions](const auto& req, auto& res) {
		CHECK_AUTH_OR_ERROR(req, res, sessions);

		LOG_INFO("Received /reset request\n");
		DynamicArray<DynamicArray<char>> cmds;
		cmds.Append(ToString("git pull"));
		cmds.Append(ToString("sudo systemctl restart npn-dev")); // Aaah! Suicide!
		for (uint64 i = 0; i < cmds.size; i++) {
			DynamicArray<char> command;
			command.Append(ToString("cd "));
			command.Append(rootPath.ToArray());
			command.Append(ToString(" && "));
			command.Append(cmds[i].ToArray());
			if (!RunCommand(command.ToArray())) {
				LOG_ERROR("Failed to run \"%.*s\" on commit request\n",
					(int)command.size, command.data);
				res.status = HTTP_STATUS_ERROR;
				return;
			}
		}
	});

	serverDev.Post("/commit", [&rootPath, &sessions](const auto& req, auto& res) {
		CHECK_AUTH_OR_ERROR(req, res, sessions);

		LOG_INFO("Received /commit request\n");
		DynamicArray<DynamicArray<char>> cmds;
		cmds.Append(ToString("git add -A"));
		auto* commitCmd = cmds.Append(ToString("git commit -m \""));
		commitCmd->Append(ToString("server commit @ "));
		time_t t = time(NULL);
		const tm* localTime = localtime(&t);
		char buffer[128];
		size_t written = strftime(buffer, 128, "%d-%m-%Y %H:%M:%S", localTime);
		if (written == 0) {
			LOG_ERROR("strftime failed on commit request\n");
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		Array<char> timestamp = { .size = written, .data = buffer };
		commitCmd->Append(timestamp);
		commitCmd->Append('"');
		cmds.Append(ToString("git push"));
		for (uint64 i = 0; i < cmds.size; i++) {
			DynamicArray<char> command;
			command.Append(ToString("cd "));
			command.Append(rootPath.ToArray());
			command.Append(ToString(" && "));
			command.Append(cmds[i].ToArray());
			if (!RunCommand(command.ToArray())) {
				LOG_ERROR("Failed to run \"%.*s\" on commit request\n",
					(int)command.size, command.data);
				res.status = HTTP_STATUS_ERROR;
				return;
			}
		}
	});

	serverDev.Post("/deploy", [&rootPath, &sessions](const auto& req, auto& res) {
		CHECK_AUTH_OR_ERROR(req, res, sessions);

		LOG_INFO("Received /deploy request\n");
		DynamicArray<DynamicArray<char>> cmds;
		cmds.Append(ToString("cd ../nopasanada && git pull"));
		cmds.Append(ToString("sudo systemctl restart npn"));
		for (uint64 i = 0; i < cmds.size; i++) {
			DynamicArray<char> command;
			command.Append(ToString("cd "));
			command.Append(rootPath.ToArray());
			command.Append(ToString(" && "));
			command.Append(cmds[i].ToArray());
			if (!RunCommand(command.ToArray())) {
				LOG_ERROR("Failed to run \"%.*s\" on commit request\n",
					(int)command.size, command.data);
				res.status = HTTP_STATUS_ERROR;
				return;
			}
		}
	});

	publicPath = rootPath;
	publicPath.Append(ToString("data/public-dev"));
	publicPath.Append('\0');
	if (!serverDev.set_base_dir(publicPath.data)) {
		LOG_ERROR("serverDev set_base_dir failed on dir %s\n", publicPath.data);
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
#include <xxhash.c>

#undef GAME_SLOW
#undef GAME_INTERNAL
#include <km_common/km_debug.h>
#undef DEBUG_ASSERTF
#undef DEBUG_ASSERT
#undef DEBUG_PANIC
#define DEBUG_ASSERTF(expression, format, ...) if (!(expression)) { \
	LOG_ERROR("Assertion failed (file %s, line %d, function %s):\n", __FILE__, __LINE__, __func__); \
	LOG_ERROR(format, ##__VA_ARGS__); \
	abort(); }
#define DEBUG_ASSERT(expression) DEBUG_ASSERTF(expression, "%s\n", "No message.")
#define DEBUG_PANIC(format, ...) \
	LOG_ERROR("PANIC! (file %s, line %d, function %s)\n", __FILE__, __LINE__, __func__); \
	LOG_ERROR(format, ##__VA_ARGS__); \
	abort();

#include <km_common/km_kmkv.cpp>
#include <km_common/km_lib.cpp>
#include <km_common/km_memory.cpp>
#include <km_common/km_os.cpp>
#include <km_common/km_string.cpp>

