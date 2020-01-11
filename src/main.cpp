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
internal bool SearchAndReplace(const Array<char>& string,
	const HashTable<DynamicArray<char, Allocator>>& items, DynamicArray<char, Allocator>* outString)
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
					const DynamicArray<char, Allocator>* replaceValuePtr = items.GetValue(replaceKey);
					if (replaceValuePtr == nullptr) {
						fprintf(stderr, "Failed to find replace key %.*s\n",
							(int)replaceKey.string.size, replaceKey.string.data);
						return false;
					}
					const DynamicArray<char, Allocator>& replaceValue = *replaceValuePtr;
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

		const DynamicArray<char, StandardAllocator>* type = GetKmkvItemStrValue(kmkv, "type");
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

		HashTable<DynamicArray<char>> templateItems;
		templateItems.Add("description", ToString("description"));
		templateItems.Add("url", ToString("/content/202001/que-paso-venezuela"));
		templateItems.Add("image", ToString("/images/202001/que-paso-venezuela.jpg"));
		templateItems.Add("color", ToString("#ff0000"));
		templateItems.Add("title", ToString("The Title of the Article"));
		templateItems.Add("subtitle", ToString("The article's subtitle, and it's probably longer than the title."));
		templateItems.Add("subtextLeft", ToString("POR JOSE M RICO"));
		templateItems.Add("subtextRight", ToString("10 DE ENERO"));
		templateItems.Add("text", ToString("<p><strong>&iquest;Que ha pasado?</strong></p>"
"<p>Este domingo 5 de enero se ten&iacute;a prevista la realizaci&oacute;n de la elecci&oacute;n de la nueva junta directiva de la Asamblea Nacional (AN) de Venezuela. Juan Guaid&oacute;, quien hasta entonces ejerc&iacute;a la Presidencia del ente legislativo presentaba su nombre a la reelecci&oacute;n.</p>"
"<p>La AN est&aacute; compuesta por un total de 167 diputados, y su Reglamento Interior y de Debate estipula que el qu&oacute;rum reglamentario para proceder a tal escogencia es de, al menos, la mitad m&aacute;s uno del total existente (85).&nbsp;</p>"
"<p>En un clima marcado por la opacidad informativa y el bloqueo a los accesos del hemiciclo de sesiones de la AN, protagonizado por miembros de la Guardia Nacional Bolivariana (GNB), los diputados Luis Parra, Franklyn Duarte y Jos&eacute; Gregorio Noriega (opuestos a Guaid&oacute; y vinculados a esc&aacute;ndalos de corrupci&oacute;n y compra de voluntades por parte del chavismo) afirmaron alzarse con la mayor&iacute;a de los votos en la c&aacute;mara, para erigirse as&iacute; &ndash;pr&aacute;cticamente a la fuerza y contando incluso con los votos de la bancada del Partido Socialista Unido de Venezuela con m&aacute;s de 40 legisladores - como nuevos directivos del parlamento.&nbsp;</p>"
"<p>En respuesta, y ante la imposibilidad de ingresar a la Asamblea, Juan Guaid&oacute; se traslad&oacute; junto a un grupo de diputados a la Sede del Diario El Nacional, en donde procedi&oacute; a realizar la votaci&oacute;n para la escogencia de la junta directiva.</p>"
"<p>En este acto se incorpor&oacute;, a trav&eacute;s del voto electr&oacute;nico a distancia, a m&aacute;s de 20 diputados que al d&iacute;a de hoy est&aacute;n fuera de Venezuela por persecuci&oacute;n pol&iacute;tica, sumando as&iacute; 100 diputados a favor de la reelecci&oacute;n de Juan Guaid&oacute; como presidente, as&iacute; como la elecci&oacute;n de Juan Pablo Guanipa y Carlos Berrizbeitia en las vicepresidencias del ente legislativo venezolano.&nbsp;</p>"
"<p>Al llegar el martes 7 de enero el propio Guaid&oacute; y los diputados que le respaldan optaron por desplazarse al Capitolio, logrando superar el piquete militar para entrar a empellones al Hemiciclo de sesiones.</p>"
"<p>Parra y los otros parlamentarios que apoyaron su apuntalamiento como Presidente de la AN (incluyendo a los del chavismo) optaron por retirarse r&aacute;pidamente del sitio. Del n&uacute;mero de apoyos con los que cuenta hasta el d&iacute;a de hoy no hay claridad. Sin embargo, Guaid&oacute; ha logrado sumar con quienes est&aacute;n en Venezuela y fuera de ella un n&uacute;mero redondo de 100 Diputados favorables a su reelecci&oacute;n.&nbsp;</p>"
"<p><strong>&iquest;Qui&eacute;n controlar&aacute; en definitiva el Hemiciclo en las pr&oacute;ximas semanas?</strong></p>"
"<p>Aunque Guaid&oacute; parece superar en el conteo a la maniobra chavista para tomar el control del parlamento, la realidad nos habla de que el manejo del mismo puede depender, en lo sucesivo, m&aacute;s de un asunto de fuerza pura y dura que de apego a la l&oacute;gica y los reglamentos.</p>"
"<p>Veremos&hellip;</p>"));

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