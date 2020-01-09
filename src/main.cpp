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

struct DynamicStringOrHashTable
{
	bool isString;
	union {
		DynamicArray<char>* dynamicStringPtr;
		HashTable<DynamicStringOrHashTable>* hashTablePtr;
	};
};

internal bool LoadKmkv(const char* filePath, HashTable<DynamicStringOrHashTable>* outHashTable)
{
	outHashTable->Clear();

	Array<uint8> kmkvFile;
	if (!LoadEntireFile(filePath, &standardAllocator_, &kmkvFile)) {
		fprintf(stderr, "Failed to load file %s\n", filePath);
		return false;
	}
	defer(FreeFile(kmkvFile, &standardAllocator_));

	Array<char> fileString;
	fileString.size = kmkvFile.size;
	fileString.data = (char*)kmkvFile.data;
	FixedArray<char, KEYWORD_MAX_LENGTH> keyword;
	FixedArray<char, VALUE_MAX_LENGTH> value;
	while (true) {
		int read = ReadNextKeywordValue(fileString, &keyword, &value);
		if (read < 0) {
			fprintf(stderr, "kmkv file keyword/value error (%s)\n", filePath);
			return false;
		}
		else if (read == 0) {
			break;
		}
		fileString.size -= read;
		fileString.data += read;

		printf("Keyword: %.*s\n", (int)keyword.size, keyword.data);
		printf("Value: %.*s\n", (int)value.size, value.data);
	}

	return true;
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

	HashTable<DynamicStringOrHashTable> test;
	if (!LoadKmkv("./data/content/202001/que-paso-venezuela.kmkv", &test)) {
		fprintf(stderr, "LoadKmkv failed\n");
		return 1;
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