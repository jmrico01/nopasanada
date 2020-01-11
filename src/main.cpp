#include <cassert>
#include <fstream>
#include <httplib.h>
#include <ios>
#include <stdio.h>

#include <km_common/km_defines.h>
#include <km_common/km_lib.h>
#include <km_common/km_os.h>
#include <km_common/km_string.h>

global_var const int HTTP_STATUS_OK    = 200;
global_var const int HTTP_STATUS_ERROR = 500;

global_var const int SERVER_PORT = 6060;

global_var const uint64 VALUE_MAX_LENGTH = KILOBYTES(32);
thread_local FixedArray<char, VALUE_MAX_LENGTH> kmkvValue_;

template <typename Allocator>
internal bool SearchAndReplace(const Array<char>& string, const HashTable<Array<char>>& items,
	DynamicArray<char, Allocator>* outString)
{
	outString->Clear();

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
					const Array<char>& replaceValue = *replaceValuePtr;
					// TODO replace with Append(array)
					for (uint64 j = 0; j < replaceValue.size; j++) {
						outString->Append(replaceValue[j]);
					}

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

template <typename Allocator = StandardAllocator>
struct KmkvItem
{
	DynamicArray<char, Allocator> keywordTag;
	bool isString;
	DynamicArray<char, Allocator>* dynamicStringPtr;
	HashTable<KmkvItem<Allocator>>* hashTablePtr;
};

template <typename Allocator>
const DynamicArray<char, Allocator>* GetKmkvItemStrValue(
	const HashTable<KmkvItem<Allocator>>& kmkv, const HashKey& itemKey)
{
	const KmkvItem<Allocator>* itemValuePtr = kmkv.GetValue(itemKey);
	if (itemValuePtr == nullptr) {
		return nullptr;
	}
	if (!itemValuePtr->isString) {
		return nullptr;
	}
	return itemValuePtr->dynamicStringPtr;
}

template <typename Allocator>
const HashTable<KmkvItem<Allocator>>* GetKmkvItemObjValue(
	const HashTable<KmkvItem<Allocator>>& kmkv, const HashKey& itemKey)
{
	KmkvItem<Allocator>* itemValuePtr = kmkv.GetValue(itemKey);
	if (itemValuePtr == nullptr) {
		return nullptr;
	}
	if (itemValuePtr->isString) {
		return nullptr;
	}
	return itemValuePtr->hashTablePtr;
}

template <typename Allocator>
internal bool LoadKmkvRecursive(Array<char> string, Allocator* allocator,
	HashTable<KmkvItem<Allocator>>* outHashTable)
{
	const uint64 KEYWORD_MAX_LENGTH = 32;
	FixedArray<char, KEYWORD_MAX_LENGTH> keyword;
	KmkvItem<Allocator> kmkvValueItem;
	while (true) {
		// TODO sometimes string changes after this function call. Hmmm...
		// Maybe it's when value and string are pointing to the same buffer area thing.
		int read = ReadNextKeywordValue(string, &keyword, &kmkvValue_);
		if (read < 0) {
			fprintf(stderr, "kmkv file keyword/value error\n");
			return false;
		}
		else if (read == 0) {
			break;
		}
		string.size -= read;
		string.data += read;
		printf("keyword %.*s, value %.*s\n", (int)keyword.size, keyword.data,
			(int)kmkvValue_.size, kmkvValue_.data);

		kmkvValueItem.keywordTag.Clear();
		bool keywordHasTag = false;
		uint64 keywordTagInd = 0;
		while (keywordTagInd < keyword.size) {
			if (keyword[keywordTagInd] == '{') {
				keywordHasTag = true;
				break;
			}
			keywordTagInd++;
		}
		if (keywordHasTag) {
			keywordTagInd++;
			bool bracketMatched = false;
			while (keywordTagInd < keyword.size) {
				if (keyword[keywordTagInd] == '}') {
					bracketMatched = true;
					break;
				}
				kmkvValueItem.keywordTag.Append(keyword[keywordTagInd++]);
			}
			if (!bracketMatched) {
				fprintf(stderr, "kmkv keyword tag unmatched bracket\n");
				return false;
			}
			if (bracketMatched && keywordTagInd != keyword.size - 1) {
				fprintf(stderr, "found characters after kmkv keyword tag bracket, keyword %.*s\n",
					(int)keyword.size, keyword.data);
				return false;
			}
		}
		if (StringCompare(kmkvValueItem.keywordTag.ToArray(), "kmkv")) {
			kmkvValueItem.isString = false;
			// "placement new" - allocate with custom allocator, but still call constructor
			kmkvValueItem.hashTablePtr = allocator->New<HashTable<KmkvItem<Allocator>>>();
			if (kmkvValueItem.hashTablePtr == nullptr) {
				return false;
			}
			new (kmkvValueItem.hashTablePtr) HashTable<KmkvItem<Allocator>>();
			LoadKmkvRecursive(kmkvValue_.ToArray(), allocator, kmkvValueItem.hashTablePtr);
		}
		else {
			kmkvValueItem.isString = true;
			// "placement new" - allocate with custom allocator, but still call constructor
			kmkvValueItem.dynamicStringPtr = allocator->New<DynamicArray<char>>();
			if (kmkvValueItem.dynamicStringPtr == nullptr) {
				return false;
			}
			new (kmkvValueItem.dynamicStringPtr) DynamicArray<char>(kmkvValue_.ToArray());
		}
		Array<char> keywordArray = keyword.ToArray();
		if (keywordHasTag) {
			keywordArray = keywordArray.SliceTo(keywordArray.size - 2 - kmkvValueItem.keywordTag.size);
		}
		outHashTable->Add(keywordArray, kmkvValueItem);
	}

	return true;
}

template <typename Allocator>
internal bool LoadKmkv(const Array<char>& filePath, Allocator* allocator,
	HashTable<KmkvItem<Allocator>>* outHashTable)
{
	Array<uint8> kmkvFile = LoadEntireFile(filePath, allocator);
	if (kmkvFile.data == nullptr) {
		fprintf(stderr, "Failed to load file %.*s\n", (int)filePath.size, filePath.data);
		return false;
	}
	defer(FreeFile(kmkvFile, allocator));

	Array<char> fileString;
	fileString.size = kmkvFile.size;
	fileString.data = (char*)kmkvFile.data;

	outHashTable->Clear();
	return LoadKmkvRecursive(fileString, allocator, outHashTable);
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

	FixedArray<char, PATH_MAX_LENGTH> publicPath = rootPath;
	publicPath.Append(ToString("data/public"));
	publicPath.Append('\0');
	bool result = httpServer.set_base_dir(publicPath.data);
	if (!result) {
		fprintf(stderr, "set_base_dir failed on dir %s\n", publicPath.data);
		return 1;
	}

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

	httpServer.Get("/content/[^/]+/.+", [rootPath](const httplib::Request& req, httplib::Response& res) {
		std::string filePath = "./data" + req.path;
		if (filePath[filePath.size() - 1] == '/') {
			filePath.pop_back();
		}
		filePath += ".kmkv";

		HashTable<KmkvItem<StandardAllocator>> kmkv;
		Array<char> filePathArray = ToString(filePath.c_str());
		if (!LoadKmkv(filePathArray, &defaultAllocator_, &kmkv)) {
			fprintf(stderr, "LoadKmkv failed for %.*s\n",
				(int)filePathArray.size, filePathArray.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}

		const auto* type = GetKmkvItemStrValue(kmkv, "type");
		if (type == nullptr) {
			fprintf(stderr, "Entry missing string \"type\": %.*s\n",
				(int)filePathArray.size, filePathArray.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}

		FixedArray<char, PATH_MAX_LENGTH> templatePath = rootPath;
		templatePath.Append(ToString("data/content/templates/"));
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
		Array<char> templateString;
		templateString.data = (char*)templateFile.data;
		templateString.size = templateFile.size;

		HashTable<Array<char>> templateItems;
		templateItems.Add("url", ToString(req.path.c_str()));

		const auto* media = GetKmkvItemStrValue(kmkv, "media");
		templateItems.Add("image", ToString("/images/202001/que-paso-venezuela.jpg"));

		const auto* description = GetKmkvItemStrValue(kmkv, "description");
		if (description == nullptr) {
			fprintf(stderr, "Entry missing string \"description\": %.*s\n",
				(int)filePathArray.size, filePathArray.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		templateItems.Add("description", description->ToArray());

		const auto* color = GetKmkvItemStrValue(kmkv, "color");
		if (color == nullptr) {
			fprintf(stderr, "Entry missing string \"color\": %.*s\n",
				(int)filePathArray.size, filePathArray.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		templateItems.Add("color", color->ToArray());

		const auto* title = GetKmkvItemStrValue(kmkv, "title");
		if (title == nullptr) {
			fprintf(stderr, "Entry missing string \"title\": %.*s\n",
				(int)filePathArray.size, filePathArray.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		templateItems.Add("title", title->ToArray());

		const auto* subtitle = GetKmkvItemStrValue(kmkv, "subtitle");
		if (subtitle == nullptr) {
			fprintf(stderr, "Entry missing string \"subtitle\": %.*s\n",
				(int)filePathArray.size, filePathArray.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		templateItems.Add("subtitle", subtitle->ToArray());

		const auto* day = GetKmkvItemStrValue(kmkv, "day");
		if (day == nullptr) {
			fprintf(stderr, "Entry missing string \"day\": %.*s\n",
				(int)filePathArray.size, filePathArray.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		const auto* month = GetKmkvItemStrValue(kmkv, "month");
		if (month == nullptr) {
			fprintf(stderr, "Entry missing string \"month\": %.*s\n",
				(int)filePathArray.size, filePathArray.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		const auto* year = GetKmkvItemStrValue(kmkv, "year");
		if (year == nullptr) {
			fprintf(stderr, "Entry missing string \"year\": %.*s\n",
				(int)filePathArray.size, filePathArray.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		templateItems.Add("subtextRight", ToString("10 DE ENERO")); // TODO build date

		const auto* author = GetKmkvItemStrValue(kmkv, "author"); // TODO different per entry type
		if (author == nullptr) {
			fprintf(stderr, "Entry missing string \"author\": %.*s\n",
				(int)filePathArray.size, filePathArray.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		templateItems.Add("subtextLeft", ToString("POR JOSE M RICO")); // TODO build this

		const auto* text = GetKmkvItemStrValue(kmkv, "text"); // TODO different per entry type
		if (text == nullptr) {
			fprintf(stderr, "Entry missing string \"text\": %.*s\n",
				(int)filePathArray.size, filePathArray.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}
		templateItems.Add("text", text->ToArray());

		DynamicArray<char> outString;
		if (!SearchAndReplace(templateString, templateItems, &outString)) {
			fprintf(stderr, "Failed to search-and-replace to template file %.*s\n",
				(int)templatePath.size, templatePath.data);
			res.status = HTTP_STATUS_ERROR;
			return;
		}

		res.set_content(outString.data, outString.size, "text/html");
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
#define DEBUG_ASSERT(expression) DEBUG_ASSERTF(expression, "")
#define DEBUG_PANIC(format, ...) \
    fprintf(stderr, "PANIC!\n"); \
    fprintf(stderr, format, ##__VA_ARGS__); \
    abort();

#define LOG_ERROR(format, ...) fprintf(stderr, format, ##__VA_ARGS__)

#include <km_common/km_lib.cpp>
#include <km_common/km_memory.cpp>
#include <km_common/km_os.cpp>
#include <km_common/km_string.cpp>