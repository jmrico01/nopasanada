from env_settings import WIN32_VCVARSALL

class LibExternal:
	def __init__(self, name, path, compiledNames = None, dllNames = None):
		self.name = name
		self.path = path
		self.compiledNames = compiledNames
		self.dllNames = dllNames

PROJECT_NAME = "nopasanada"

DEPLOY_FILES = []

LIBS_EXTERNAL = []

PATHS = {
	"win32-vcvarsall": WIN32_VCVARSALL
}
