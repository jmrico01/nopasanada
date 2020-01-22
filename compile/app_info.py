from env_settings import DEFINES_ENV, WIN32_VCVARSALL

class LibExternal:
	def __init__(self, name, path, compiledNames = None, dllNames = None):
		self.name = name
		self.path = path
		self.compiledNames = compiledNames
		self.dllNames = dllNames

PROJECT_NAME = "nopasanada"

COPY_DIRS = {}

DEFINES = [
	"KM_KMKV_JSON",
	"KM_UTF8"
]
DEFINES.extend(DEFINES_ENV)

DEPLOY_FILES = []

LIBS_EXTERNAL = [
	LibExternal("cJSON", "cJSON"),
	LibExternal("cpp-httplib", "cpp-httplib"),
	LibExternal("stb_sprintf", "stb_sprintf-1.06"),
	LibExternal("utf8proc",    "utf8proc")
]

PATHS = {
	"win32-vcvarsall": WIN32_VCVARSALL
}
