from compile import Platform, TargetType, Define, PlatformTargetOptions, BuildTarget, CopyDir, LibExternal
from env_settings import DEFINES_ENV, WIN32_VCVARSALL

windows_options = PlatformTargetOptions(
    defines=[],
    compiler_flags=[
        "/MTd",

        # "/wd4100", # unreferenced formal parameter
        "/wd4201", # nonstandard extension used: nameless struct/union
    ],
    linker_flags=[]
)

linux_options = PlatformTargetOptions(
    defines=[],
    compiler_flags=[],
    linker_flags=[
        "-lz",
        "-lssl",
        "-lcrypto"
    ]
)

mac_options = PlatformTargetOptions(
    defines=[],
    compiler_flags=[],
    linker_flags=[]
)

TARGETS = [
    BuildTarget("nopasanada",
        source_file="src/main.cpp",
        type=TargetType.EXECUTABLE,
        defines=[
            Define("KM_CPP_STD"),
            Define("KM_KMKV_JSON"),
            Define("KM_UTF8"),
        ],
        platform_options={
            Platform.WINDOWS: windows_options,
            Platform.LINUX: linux_options
        }
    )
]

TARGETS[0].defines.extend(DEFINES_ENV)

COPY_DIRS = [
    CopyDir("data", "data")
]

DEPLOY_FILES = []

LIBS_EXTERNAL = [
    LibExternal("cJSON",       path="cJSON"),
    LibExternal("cpp-httplib", path="cpp-httplib"),
    LibExternal("stb_sprintf", path="stb_sprintf-1.06"),
    LibExternal("utf8proc",    path="utf8proc"),
    LibExternal("xxHash",      path="xxHash")
]

PATHS = {
    "win32-vcvarsall": WIN32_VCVARSALL
}

def post_compile_custom(paths):
    pass
