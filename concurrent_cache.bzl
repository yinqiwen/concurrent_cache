load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")

def clean_dep(dep):
    return str(Label(dep))

def concurrent_cache_workspace(path_prefix = "", tf_repo_name = "", **kwargs):
    http_archive(
        name = "bazel_skylib",
        urls = [
            "https://github.com/bazelbuild/bazel-skylib/releases/download/1.0.3/bazel-skylib-1.0.3.tar.gz",
            "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/1.0.3/bazel-skylib-1.0.3.tar.gz",
        ],
        sha256 = "1c531376ac7e5a180e0237938a2536de0c54d93f5c278634818e0efc952dd56c",
    )
    http_archive(
        name = "rules_cc",
        sha256 = "35f2fb4ea0b3e61ad64a369de284e4fbbdcdba71836a5555abb5e194cf119509",
        strip_prefix = "rules_cc-624b5d59dfb45672d4239422fa1e3de1822ee110",
        urls = [
            "https://mirror.bazel.build/github.com/bazelbuild/rules_cc/archive/624b5d59dfb45672d4239422fa1e3de1822ee110.tar.gz",
            "https://github.com/bazelbuild/rules_cc/archive/624b5d59dfb45672d4239422fa1e3de1822ee110.tar.gz",
        ],
    )

    # rules_proto defines abstract rules for building Protocol Buffers.
    http_archive(
        name = "rules_proto",
        sha256 = "2490dca4f249b8a9a3ab07bd1ba6eca085aaf8e45a734af92aad0c42d9dc7aaf",
        strip_prefix = "rules_proto-218ffa7dfa5408492dc86c01ee637614f8695c45",
        urls = [
            "https://mirror.bazel.build/github.com/bazelbuild/rules_proto/archive/218ffa7dfa5408492dc86c01ee637614f8695c45.tar.gz",
            "https://github.com/bazelbuild/rules_proto/archive/218ffa7dfa5408492dc86c01ee637614f8695c45.tar.gz",
        ],
    )

    http_archive(
        name = "rules_foreign_cc",
        strip_prefix = "rules_foreign_cc-0.9.0",
        url = "https://github.com/bazelbuild/rules_foreign_cc/archive/0.9.0.zip",
    )

    http_archive(
        name = "rules_license",
        urls = [
            "https://mirror.bazel.build/github.com/bazelbuild/rules_license/releases/download/0.0.8/rules_license-0.0.8.tar.gz",
            "https://github.com/bazelbuild/rules_license/releases/download/0.0.8/rules_license-0.0.8.tar.gz",
        ],
        sha256 = "241b06f3097fd186ff468832150d6cc142247dc42a32aaefb56d0099895fd229",
    )

    gtest_ver = kwargs.get("gtest_ver", "1.14.0")
    gtest_name = "googletest-{ver}".format(ver = gtest_ver)
    maybe(
        http_archive,
        name = "com_google_googletest",
        strip_prefix = gtest_name,
        urls = [
            "https://github.com/google/googletest/archive/refs/tags/v{ver}.tar.gz".format(ver = gtest_ver),
        ],
    )

    _FMTLIB_BUILD_FILE = """
load("@rules_foreign_cc//foreign_cc:defs.bzl", "cmake")
filegroup(
    name = "all_srcs",
    srcs = glob(["**"]),
    visibility = ["//visibility:public"],
)

cmake(
    name = "fmt",
    generate_args = [
        "-DCMAKE_BUILD_TYPE=Release",
        "-DFMT_TEST=OFF",
        "-DFMT_DOC=OFF",
    ],
    lib_source = ":all_srcs",
    out_lib_dir = "lib64",
    out_static_libs = [
        "libfmt.a",
    ],
    visibility = ["//visibility:public"],
)
"""
    fmtlib_ver = kwargs.get("fmtlib_ver", "11.0.2")
    fmtlib_name = "fmt-{ver}".format(ver = fmtlib_ver)

    maybe(
        http_archive,
        name = "com_github_fmtlib",
        strip_prefix = fmtlib_name,
        urls = [
            "https://github.com/fmtlib/fmt/archive/refs/tags/{ver}.tar.gz".format(ver = fmtlib_ver),
        ],
        build_file_content = _FMTLIB_BUILD_FILE,
    )

    _SPDLOG_BUILD_FILE = """
cc_library(
    name = "spdlog",
    hdrs = glob([
        "include/**/*.h",
    ]),
    srcs= glob([
        "src/*.cpp",
    ]),
    defines = ["SPDLOG_FMT_EXTERNAL", "SPDLOG_COMPILED_LIB"],
    includes = ["include"],
    deps = ["@com_github_fmtlib//:fmt"],
    visibility = ["//visibility:public"],
)
"""
    spdlog_ver = kwargs.get("spdlog_ver", "1.14.1")
    spdlog_name = "spdlog-{ver}".format(ver = spdlog_ver)
    maybe(
        http_archive,
        name = "spdlog",
        strip_prefix = spdlog_name,
        urls = [
            "https://github.com/gabime/spdlog/archive/v{ver}.tar.gz".format(ver = spdlog_ver),
        ],
        build_file_content = _SPDLOG_BUILD_FILE,
    )

    abseil_ver = kwargs.get("abseil_ver", "20240116.2")
    abseil_name = "abseil-cpp-{ver}".format(ver = abseil_ver)
    maybe(
        http_archive,
        name = "com_google_absl",
        strip_prefix = abseil_name,
        urls = [
            "https://github.com/abseil/abseil-cpp/archive/refs/tags/{ver}.tar.gz".format(ver = abseil_ver),
        ],
    )

    maybe(
        git_repository,
        name = "com_google_highway",
        remote = "https://github.com/google/highway.git",
        tag = "1.2.0",
    )

    bench_ver = kwargs.get("bench_ver", "1.8.3")
    bench_name = "benchmark-{ver}".format(ver = bench_ver)
    maybe(
        http_archive,
        name = "com_google_benchmark",
        strip_prefix = bench_name,
        urls = [
            "https://github.com/google/benchmark/archive/v{ver}.tar.gz".format(ver = bench_ver),
        ],
    )

    _CQ_BUILD_FILE = """
cc_library(
    name = "concurrentqueue",
    hdrs = glob([
        "**/*.h",
    ]),
    includes=["./"],
    visibility = [ "//visibility:public" ],
)
"""
    concurrentqueue_ver = kwargs.get("concurrentqueue_ver", "1.0.4")
    concurrentqueue_name = "concurrentqueue-{ver}".format(ver = concurrentqueue_ver)
    http_archive(
        name = "com_github_cameron314_concurrentqueue",
        strip_prefix = concurrentqueue_name,
        build_file_content = _CQ_BUILD_FILE,
        urls = [
            "https://mirrors.tencent.com/github.com/cameron314/concurrentqueue/archive/v{ver}.tar.gz".format(ver = concurrentqueue_ver),
            "https://github.com/cameron314/concurrentqueue/archive/refs/tags/v{ver}.tar.gz".format(ver = concurrentqueue_ver),
        ],
    )

    _JEMALLOC_BUILD_CONTENT = """
package(default_visibility = ["//visibility:public"])

load("@rules_foreign_cc//foreign_cc:defs.bzl", "configure_make")

filegroup(
    name = "all_srcs",
    srcs = glob(["**"]),
    visibility = ["//visibility:public"],
)

configure_make(
    name = "jemalloc_build",
    args = ["-j 6"],
    autogen = True,
    configure_in_place = True,
    # configure_options = [
    #     "--enable-prof",
    #     "--enable-prof-libunwind",
    # ],
    lib_source = ":all_srcs",
    # out_binaries = ["jeprof"],
    out_static_libs = ["libjemalloc.a"],
)

cc_library(
    name = "jemalloc",
    tags = ["no-cache"],
    deps = [
        ":jemalloc_build",
    ],
)
"""

    jemalloc_ver = kwargs.get("jemalloc_ver", "5.3.0")
    jemalloc_name = "jemalloc-{ver}".format(ver = jemalloc_ver)
    http_archive(
        name = "com_github_jemalloc",
        strip_prefix = jemalloc_name,
        urls = [
            # "https://github.com/jemalloc/jemalloc/archive/{commit}.zip".format(commit = jemalloc_commit),
            "https://github.com/jemalloc/jemalloc/releases/download/{ver}/jemalloc-{ver}.tar.bz2".format(ver = jemalloc_ver),
        ],
        build_file_content = _JEMALLOC_BUILD_CONTENT,
    )
