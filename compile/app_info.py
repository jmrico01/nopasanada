from env_settings import DEFINES_ENV, WIN32_VCVARSALL
import os
import shutil

class LibExternal:
	def __init__(self, name, path, compiledNames = None, dllNames = None):
		self.name = name
		self.path = path
		self.compiledNames = compiledNames
		self.dllNames = dllNames

PROJECT_NAME = "nopasanada"

COPY_DIRS = {}

DEFINES = [
	"KM_CPP_STD",
	"KM_KMKV_JSON",
	"KM_UTF8"
]
DEFINES.extend(DEFINES_ENV)

DEPLOY_FILES = []

LIBS_EXTERNAL = [
	LibExternal("cJSON",       "cJSON"),
	LibExternal("cpp-httplib", "cpp-httplib"),
	LibExternal("stb_sprintf", "stb_sprintf-1.06"),
	LibExternal("utf8proc",    "utf8proc"),
	LibExternal("xxHash",      "xxHash")
]

PATHS = {
	"win32-vcvarsall": WIN32_VCVARSALL
}

USE_KM_PLATFORM = False

# TODO copied from compile.py
def remake_dest_and_copy_dir(src_path, dst_path):
	# Re-create (clear) the directory
	if os.path.exists(dst_path):
		shutil.rmtree(dst_path)
	os.makedirs(dst_path)

	# Copy
	for file_name in os.listdir(src_path):
		file_path = os.path.join(src_path, file_name)
		if os.path.isfile(file_path):
			shutil.copy2(file_path, dst_path)
		elif os.path.isdir(file_path):
			shutil.copytree(file_path, os.path.join(dst_path, file_name))

def make_and_clear_dir(path):
    if not os.path.exists(path):
        os.makedirs(path)

    for file_name in os.listdir(path):
        file_path = os.path.join(path, file_name)
        try:
            if os.path.isfile(file_path):
                os.remove(file_path)
            elif os.path.isdir(file_path):
                shutil.rmtree(file_path)
        except Exception as e:
            print("Failed to clean {}: {}".format(file_path, str(e)))

def post_compile_custom(paths):
	path_react = os.path.join(paths["root"], "react")
	path_react_build = os.path.join(path_react, "build")
	make_and_clear_dir(path_react_build)
	os.system(" & ".join([
		"pushd " + path_react,
		"npm i",
		"npm run build",
		"popd"
	]))
	path_build_public = os.path.join(paths["build"], "public")
	remake_dest_and_copy_dir(path_react_build, path_build_public)
