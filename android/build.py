#!/usr/bin/env python3

import os
import subprocess
import sys
import shutil
import glob
import json
import argparse

def main(deploy=False, validation=False):

    SDK_VERSION = "android-23"
    APK_NAME = "vulkanglTFPBR"

    # Check if python 3, python 2 not supported
    if sys.version_info <= (3, 0):
        print("Sorry, requires Python 3.x, not Python 2.x")
        return False

    # Check if a build file is present, if not create one using the android SDK version specified
    if not os.path.isfile("build.xml"):
        print("Build.xml not present, generating with %s " % SDK_VERSION)
        ANDROID_CMD = "android"
        if os.name == 'nt':
            ANDROID_CMD += ".bat"
        if subprocess.call(("%s update project -p ./%s -t %s" % (ANDROID_CMD, "./", SDK_VERSION)).split(' ')) != 0:
            print("Error: Project update failed!")
            return False

    # Enable validation
    BUILD_ARGS = ""

    if validation:
        # Use a define to force validation in code
        BUILD_ARGS = "APP_CFLAGS=-D_VALIDATION"

    # Verify submodules are loaded in external folder
    if not os.listdir("../external/glm/") or not os.listdir("../external/gli/"):
        print("External submodules not loaded. Clone them using:")
        print("\tgit submodule init\n\tgit submodule update")
        return False

    # Build
    old_cwd = os.getcwd()

    if subprocess.call("ndk-build %s" %BUILD_ARGS, shell=True) == 0:
        print("Build successful")

        if validation:
            # Copy validation layers
            # todo: Currently only arm v7
            print("Validation enabled, copying validation layers...")
            os.makedirs("./libs/armeabi-v7a", exist_ok=True)
            for file in glob.glob("../layers/armeabi-v7a/*.so"):
                print("\t" + file)
                shutil.copy(file, "./libs/armeabi-v7a")

        # Assets
        shutil.rmtree("./assets")        
        shutil.copytree("../data/", "./assets/")

        if subprocess.call("ant debug -Dout.final.file=%s.apk" % APK_NAME, shell=True) == 0:
            if deploy and subprocess.call("adb install -r %s.apk" % APK_NAME, shell=True) != 0:
                print("Could not deploy to device!")
        else:
            print("Error during build process!")
            return False
    else:
        print("Error building project!")
        return False

    # Copy apk to bin folder
    os.makedirs("../bin", exist_ok=True)
    shutil.move('%s.apk' % APK_NAME, "../bin/%s.apk" % APK_NAME)
    os.chdir(old_cwd)
    return True

class ReadableDir(argparse.Action):
    def __call__(self, parser, namespace, values, option_string=None):
        if not os.path.isdir(values):
            raise argparse.ArgumentTypeError("{0} is not a valid path".format(values))
        if not os.access(values, os.R_OK):
            raise argparse.ArgumentTypeError("{0} is not a readable dir".format(values))
        setattr(namespace, self.dest,values)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="Build and deploy a single example")
    parser.add_argument('-deploy', default=False, action='store_true', help="install example on device")
    parser.add_argument('-validation', default=False, action='store_true')
    try:
        args = parser.parse_args()
    except SystemExit:
        sys.exit(1)
    ok = main(args.deploy, args.validation)
    sys.exit(0 if ok else 1)
