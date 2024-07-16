#!/usr/bin/env python
#
# Copyright 2024 - The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the',  help='License');
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an',  help='AS IS' BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import logging
import os
import shutil
import subprocess
import aemu.prebuilts.deps.common as deps_common
from pathlib import Path
import platform

AOSP_ROOT = Path(__file__).resolve().parents[7]
HOST_OS = platform.system().lower()
HOST_ARCH = platform.machine().lower()

AOSP_ANGLE_SRC_PATH = AOSP_ROOT / "external" / "angle"
DEPOT_TOOLS_DIRNAME = "_depot_tools"
DEPOT_TOOLS_PATH = AOSP_ROOT / "external" / "angle" / DEPOT_TOOLS_DIRNAME
CMAKE_PATH = os.path.join(AOSP_ROOT, "prebuilts", "cmake", HOST_OS + "-x86", "bin")
NINJA_PATH = os.path.join(AOSP_ROOT, "prebuilts", "ninja", HOST_OS + "-x86")

def genLastChangeFile():
    """The build requires a file to exist, angle/build/util. This file is normally generated by
    `gclient runhooks`, but this step fails on mac. So we need to generate it manually.
    """
    lastchange = AOSP_ANGLE_SRC_PATH / "build" / "util" / "LASTCHANGE"
    lastchange_cmd = ["python3", os.path.join("build", "util", "lastchange.py"),
                      "-o", str(lastchange)]
    logging.info(f">> CMD {lastchange_cmd}")
    res = subprocess.run(args=lastchange_cmd, cwd=AOSP_ANGLE_SRC_PATH, env=os.environ.copy())
    if res.returncode != 0:
        logging.critical(f"{lastchange_cmd} exited with non-zero code ({res.returncode})")
        exit(res.returncode)

def fetchAngleDependencies():
    if os.path.exists(DEPOT_TOOLS_PATH):
        shutil.rmtree(DEPOT_TOOLS_PATH)
    # Needed for gn, gclient
    depot_tools_cmd = ["git", "clone",
                       "https://chromium.googlesource.com/chromium/tools/depot_tools.git",
                       DEPOT_TOOLS_DIRNAME]
    logging.info(f">> CMD: {depot_tools_cmd}")
    res = subprocess.run(args=depot_tools_cmd, cwd=AOSP_ANGLE_SRC_PATH, env=os.environ.copy())
    if res.returncode != 0:
        logging.critical("depot_tools clone exited with non-zero code (%s)", res.returncode)
        exit(res.returncode)
    # checkout a version of depot_tools that's around the same age as our angle repo (may 2022)
    res = subprocess.run(args=["git", "checkout", "bd80a1be23d33ef037692b8b3d51b57292151267"],
                         cwd=DEPOT_TOOLS_PATH, env=os.environ.copy())
    # Disable depot_tools auto-update
    os.environ["DEPOT_TOOLS_UPDATE"] = "0"

    # Opt-in to gclient metrics collection to silence warnings
    gclient_opt_in_cmd = ["gclient", "metrics", "--opt-in"]
    logging.info(f">> CMD: {gclient_opt_in_cmd}")
    res = subprocess.run(args=gclient_opt_in_cmd, cwd=AOSP_ANGLE_SRC_PATH, env=os.environ.copy())

    bootstrap_cmd = ["python3", os.path.join("scripts", "bootstrap.py")]
    logging.info(f">> CMD: {bootstrap_cmd}")
    res = subprocess.run(args=bootstrap_cmd, cwd=AOSP_ANGLE_SRC_PATH, env=os.environ.copy())
    if res.returncode != 0:
        logging.critical("bootstrap.py exited with non-zero code (%s)", res.returncode)
        exit(res.returncode)

    # There will be an error, "vpython not found", but it's okay to ignore.
    gclient_cmd = ["gclient", "sync", "-j", "8", "--verbose"]
    logging.info(f">> CMD: {gclient_cmd}")
    # Run glient sync twice
    res = subprocess.run(args=gclient_cmd, cwd=AOSP_ANGLE_SRC_PATH, env=os.environ.copy())
    res = subprocess.run(args=gclient_cmd, cwd=AOSP_ANGLE_SRC_PATH, env=os.environ.copy())

    if HOST_OS == "darwin":
        # gclient runhooks will fail on mac, so we need to generate build/util/LASTCHANGE manually.
        genLastChangeFile()

def create_clang_toolchain(toolchain_dir: Path, extra_cflags, extra_cxxflags):
    """Creates a clang toolchain directory, with custom flags.

    GN build system does not accept extra CFLAGS/CXXFLAGS, so this provides a
    mechanism to do so. The GN build system assumes the clang binaries will be
    in `toolchain_dir`/bin/clang.

    Args:
        toolchain_dir (Path): The directory to create the toolchain in.
        extra_cflags (str): extra cflags passed to clang.
        extra_cxxflags (str): extra cxxflags passed to clang++.
    """
    logging.info(f"Creating clang toolchain at {toolchain_dir}")
    toolchain_bin_dir = toolchain_dir / "bin"
    os.makedirs(toolchain_bin_dir)
    clang_dir = deps_common.getClangDirectory()

    with open(toolchain_bin_dir / "clang", 'x') as f:
        f.write(f"#!/bin/sh\n{clang_dir}/bin/clang \"$@\" {extra_cflags}")
    os.chmod(toolchain_bin_dir / "clang", 0o777)

    with open(toolchain_bin_dir / "clang++", 'x') as f:
        f.write(f"#!/bin/sh\n{clang_dir}/bin/clang++ \"$@\" {extra_cxxflags}")
    os.chmod(toolchain_bin_dir / "clang++", 0o777)

    with open(toolchain_bin_dir / "llvm-ar", 'x') as f:
        f.write(f"#!/bin/sh\n{clang_dir}/bin/llvm-ar \"$@\"")
    os.chmod(toolchain_bin_dir / "llvm-ar", 0o777)

    with open(toolchain_bin_dir / "lld", 'x') as f:
        f.write(f"#!/bin/sh\n{clang_dir}/bin/lld \"$@\"")
    os.chmod(toolchain_bin_dir / "lld", 0o777)

def buildAngle(build_dir):
    """Builds angle.

    Args:
        build_dir (Path): The directory to build angle into.
    """
    gn_common_args = (
            'target_cpu="{arch}" target_os="{os}"'
            " angle_enable_vulkan=true is_debug=false is_component_build=true"
            " is_official_build=false use_custom_libcxx=false libcxx_abi_unstable=false"
            " dcheck_always_on=false use_dummy_lastchange=true")
    if HOST_OS == "linux":
        # Install the sysroot
        res = subprocess.run(
                args=["./build/linux/sysroot_scripts/install-sysroot.py", "--arch=amd64"],
                cwd=AOSP_ANGLE_SRC_PATH,
                env=os.environ.copy())
        if res.returncode != 0:
            logging.critical(f"install-sysroot.py exited with non-zero code {res.returncode}")
            exit(res.returncode)
        gn_args = (gn_common_args.format(arch="x64", os="linux"))
    elif HOST_OS == "darwin":
        # We need to provide our own clang. Since we are using our own version of clang,
        # we must disable the use of custom clang plugins that is shipped as a prebuilt
        # in chrome. See https://chromium.googlesource.com/chromium/src/+/HEAD/docs/clang.md.
        extra_cflags = (
                "-Wno-deprecated"
                " -Wno-unused-but-set-variable"
                " -Wno-unqualified-std-cast-call"
                " -Wno-unused-command-line-argument"
                " -Wno-deprecated-non-prototype"
                " -Wno-array-parameter"
                " -Wno-receiver-expr")
        toolchain_dir = Path(build_dir) / "my_toolchain"
        create_clang_toolchain(toolchain_dir, extra_cflags, extra_cflags)

        # Additional CXXFLAGS as we are using a newer version of clang
        gn_common_args += (
                ' clang_base_path="{clang_base_path}"'
                " clang_use_chrome_plugins=false"
                " use_system_xcode=true")
        gn_args = gn_common_args.format(
                arch="arm64" if HOST_ARCH == "aarch64" else "x64",
                os="mac",
                clang_base_path=str(toolchain_dir))
    gn_cmd = [
        "gn",
        "gen",
        build_dir,
        "--args=%s" % gn_args,
    ]

    res = subprocess.run(args=gn_cmd, cwd=AOSP_ANGLE_SRC_PATH, env=os.environ.copy())
    if res.returncode != 0:
        logging.critical(f"{gn_cmd} exited with non-zero code {res.returncode}")
        exit(res.returncode)

    res = subprocess.run(args=["autoninja", "-C", build_dir], cwd=AOSP_ANGLE_SRC_PATH,
                         env=os.environ.copy())
    if res.returncode != 0:
        logging.critical(f"autoninja exited with non-zero code {res.returncode}")
        exit(res.returncode)

    logging.info("Done building Angle")

def installAngle(builddir, installdir):
    """Installs the output files from `builddir` to `installdir`.

    Args:
        builddir (str): The location of the angle build directory.
        installdir (str): The location of the angle install directory.
    """
    os.makedirs(Path(installdir) / "lib")
    os.makedirs(Path(installdir) / "include")

    if HOST_OS == "linux":
        LIB_EXT = ".so"
        install_libs = [
            f"libvulkan{LIB_EXT}.1"
        ]
    elif HOST_OS == "darwin":
        LIB_EXT = ".dylib"
        install_libs = [ ]
    elif HOST_OS == "windows":
        LIB_EXT = ".dll"
        install_libs = [
            f"vulkan-1{LIB_EXT}"
        ]

    # Relative to the build directory
    install_libs += [
        f"libEGL{LIB_EXT}",
        f"libGLESv2{LIB_EXT}",
        f"libshadertranslator{LIB_EXT}",
        f"libvk_swiftshader{LIB_EXT}",
        "vk_swiftshader_icd.json",
    ]

    for lib in install_libs:
        src_lib = Path(builddir) / lib
        dst_lib = Path(installdir) / "lib" / lib
        logging.info(f"Copy {src_lib} ==> {dst_lib}")
        shutil.copyfile(src_lib, dst_lib)

    # Relative to the src directory
    src_files = [
        str(Path("src") / "libShaderTranslator" / "ShaderTranslator.h"),
    ]
    for f in src_files:
        src_file = AOSP_ANGLE_SRC_PATH / f
        filename = os.path.basename(f)
        dst_file = f"{installdir}/include/{filename}"
        logging.info(f"Copy {src_file} ==> {dst_file}")
        shutil.copyfile(src_file, dst_file)

    logging.info("Done installing Angle")
    return True

def buildPrebuilt(args, prebuilts_out_dir):
    # Use cmake from our prebuilts
    deps_common.addToSearchPath(CMAKE_PATH)
    # Use ninja from our prebuilts
    deps_common.addToSearchPath(NINJA_PATH)
    deps_common.addToSearchPath(str(DEPOT_TOOLS_PATH))
    logging.info(os.environ)

    # angle source code is in external/angle
    if not os.path.isdir(AOSP_ANGLE_SRC_PATH):
        logging.fatal("%s does not exist", AOSP_ANGLE_SRC_PATH)
        exit(-1)
    logging.info("ANGLE source: %s", AOSP_ANGLE_SRC_PATH)

    fetchAngleDependencies()

    angle_build_dir = os.path.join(args.out, "angle")
    angle_install_dir = os.path.join(prebuilts_out_dir, "angle")
    if os.path.exists(angle_install_dir):
        shutil.rmtree(angle_install_dir)

    buildAngle(angle_build_dir)
    installAngle(angle_build_dir, angle_install_dir)
    logging.info("Successfully built Angle!")