#include <fstream>
#include <httplib.h>
#include <ios>
#include <stdio.h>

#include <km_common/km_defines.h>
#include <km_common/km_lib.h>
#include <km_common/km_string.h>

global_var const int SERVER_PORT = 6060;
global_var StandardAllocator standardAllocator_;

template <typename Allocator>
internal bool LoadEntireFile(const char* filePath, Allocator* allocator, Array<uint8>* outFile)
{
	std::ifstream file(filePath, std::ios::binary | std::ios::ate);
	std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);

	outFile->data = (uint8*)allocator->Allocate(size);
	if (outFile->data == nullptr) {
		return false;
	}
	if (!file.read((char*)outFile->data, size)) {
		allocator->Free(outFile->data);
		return false;
	}

	outFile->size = size;
	return true;
}

template <typename Allocator>
internal void FreeFile(const Array<uint8>& outFile, Allocator* allocator)
{
	allocator->Free(outFile.data);
}

struct KmkvItem
{
	bool isString;
	DynamicArray<char> keywordTag;
	union {
		DynamicArray<char>* dynamicStringPtr;
		HashTable<KmkvItem>* hashTablePtr;
	};
};

template <typename Allocator>
internal bool LoadKmkvRecursive(Array<char> string, Allocator* allocator,
	HashTable<KmkvItem>* outHashTable)
{
	const uint64 KEYWORD_MAX_LENGTH = 32;
	const uint64 VALUE_MAX_LENGTH = KILOBYTES(32);
	FixedArray<char, KEYWORD_MAX_LENGTH> keyword;
	FixedArray<char, VALUE_MAX_LENGTH> value;
	while (true) {
		int read = ReadNextKeywordValue(string, &keyword, &value);
		if (read < 0) {
			fprintf(stderr, "kmkv file keyword/value error\n");
			return false;
		}
		else if (read == 0) {
			break;
		}
		string.size -= read;
		string.data += read;

		KmkvItem kmkvValueItem;

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
			kmkvValueItem.hashTablePtr = new (allocator->New<HashTable<KmkvItem>>()) HashTable<KmkvItem>();
			LoadKmkvRecursive(value.ToArray(), allocator, kmkvValueItem.hashTablePtr);
		}
		else {
			kmkvValueItem.isString = true;
			// "placement new" - allocate with custom allocator, but still call constructor
			kmkvValueItem.dynamicStringPtr = new (allocator->New<DynamicArray<char>>()) DynamicArray<char>(value.ToArray());
		}
		Array<char> keywordArray = keyword.ToArray();
		if (keywordHasTag) {
			keywordArray = keywordArray.SliceTo(keywordArray.size - 2 - kmkvValueItem.keywordTag.size);
		}
		const HashKey key(keywordArray);
		outHashTable->Add(key, kmkvValueItem);
	}

	return true;
}

template <typename Allocator>
internal bool LoadKmkv(const char* filePath, Allocator* allocator, HashTable<KmkvItem>* outHashTable)
{
	Array<uint8> kmkvFile;
	if (!LoadEntireFile(filePath, allocator, &kmkvFile)) {
		fprintf(stderr, "Failed to load file %s\n", filePath);
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

	bool result = httpServer.set_base_dir("./data/public");
	if (!result) {
		fprintf(stderr, "set_base_dir failed\n");
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

	httpServer.Get("/content/*/*", [](const httplib::Request& req, httplib::Response& res) {
		fprintf(stderr, "article get\n");
	});

	HashTable<KmkvItem> test;
	if (!LoadKmkv("./data/content/202001/que-paso-venezuela.kmkv", &standardAllocator_, &test)) {
		fprintf(stderr, "LoadKmkv failed\n");
		return 1;
	}

	const KmkvItem* contentType = test.GetValue("type");
	if (contentType->isString) {
		printf("content type: %.*s\n", (int)contentType->dynamicStringPtr->size, contentType->dynamicStringPtr->data);
	}
	const KmkvItem* featured = test.GetValue("featured");
	if (!featured->isString) {
		const HashTable<KmkvItem>* featuredKmkv = featured->hashTablePtr;
		const KmkvItem* pretitle = featuredKmkv->GetValue("pretitle");
		if (pretitle->isString) {
			printf("featured pretitle: %.*s\n", (int)pretitle->dynamicStringPtr->size, pretitle->dynamicStringPtr->data);
		}
		const KmkvItem* title = featuredKmkv->GetValue("title");
		if (title->isString) {
			printf("featured title: %.*s\n", (int)title->dynamicStringPtr->size, title->dynamicStringPtr->data);
		}
	}

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
#include <km_common/km_string.cpp>