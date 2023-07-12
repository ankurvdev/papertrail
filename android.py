import os
import pathlib
import shutil
import subprocess
import sys
from typing import Any, Dict, Optional

import cmake
import configenv
import externaltools
import generator

IMAGES = {}


class AndroidGeneratorException(Exception):
    pass


class AndroidAppHandler(cmake.Handler):
    def __init__(self, srcdir: pathlib.Path, builddir: Optional[pathlib.Path] = None, **kargs):
        super(AndroidAppHandler, self).__init__("android", srcdir, builddir)
        self.androidtools = externaltools.GetAndroidTools()
        self.android_workspace = self.GetBuildRootDir() / "android"
        self.gradle_workspace = self.android_workspace / "gradle"
        self.studio_workspace = self.android_workspace / "studio"
        self.gradle_workspace.mkdir(exist_ok=True, parents=True)
        self.studio_workspace.mkdir(exist_ok=True, parents=True)
        self._env: Dict[str, str] = {
            "ANDROID_HOME": externaltools.GetAndroidSdkRoot().as_posix(),
            "ANDROID_SDK_ROOT": externaltools.GetAndroidSdkRoot().as_posix(),
            "JAVA_HOME": externaltools.GetJavaHome().as_posix(),
            "GRADLE_USER_HOME": self.gradle_workspace.as_posix(),
            "ANDROID_SDK_HOME": self.studio_workspace.as_posix(),
            # "ANDROID_USER_HOME": self.studio_workspace.as_posix(), # CI Pipeline failure with this
            "XDG_CONFIG_HOME": self.android_workspace .as_posix(),
            # "HOME": self.android_workspace.as_posix()
        }
        self.extra_args = kargs

    def GetPermissionMap(self):
        return

    def _GeneratorAppManifestXml(self, gen: generator.Generator, config: dict[str: object]):
        manifestfilepath = "app/src/main/AndroidManifest.xml"
        manifesttemplate = """<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    android:versionCode="{version_code}"
    android:versionName="{version_full}"
> {permissionxml} <application
        android:allowBackup="true"
        android:icon="@mipmap/ic_launcher"
        android:label="@string/app_name"
        android:supportsRtl="true"
        android:usesCleartextTraffic="true"
        android:theme="@style/AppTheme">
        <activity
            android:name=".MainActivity"
            android:launchMode="singleTask"
            android:exported="true"
            android:configChanges="orientation|screenSize">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
                {usb_intent_filter}
            </intent-filter>
            {usb_device_metadata}
        </activity>
        {navigation_svc}
    </application>
</manifest>
        """
        permission_map = {
            "internet": '<uses-permission android:name="android.permission.INTERNET"/>\n',
            "bluetooth": '<uses-permission android:name="android.permission.BLUETOOTH"/>\n',
            "in-app-purchases": '<uses-permission android:name="com.android.vending.BILLING" />\n',
            "ble-server":
                '<uses-permission android:name="android.permission.BLUETOOTH_SCAN"/>\n'
                '<uses-permission android:name="android.permission.BLUETOOTH_CONNECT"/>\n'
                '<uses-permission android:name="android.permission.BLUETOOTH_ADVERTISE"/>\n',
            "location":
                '<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION"/>\n'
                '<uses-permission android:name="android.permission.ACCESS_COARSE_LOCATION"/>\n',
            "navigation":
                '<uses-permission android:name="android.permission.FOREGROUND_SERVICE_LOCATION"/>\n'
                # '<uses-permission android:name="android.permission.ACCESS_BACKGROUND_LOCATION"/>\n' # Google is strict about this
                '<uses-permission android:name="android.permission.FOREGROUND_SERVICE"/>\n',
            "gyroscope": '<uses-permission android:name="android.permission.ACTIVITY_RECOGNITION"/>\n',
            "usb": '',
            "extended-execution":
                '<uses-permission android:name="android.permission.POST_NOTIFICATIONS"/>\n'
                '<uses-permission android:name="android.permission.WAKE_LOCK"/>\n'
        }

        formatvars = {
            "usb_intent_filter": "",
            "usb_device_metadata": "",
            "version_code": config['build']['VersionCode'],
            "version_full": config['build']['VersionFull']
        }
        permissionxml = ""
        usb_intent_filter = ""
        usb_device_metadata = ""
        navigation_svc = ""
        app_config: dict[str, object] = config["application"]
        permissions: set[str] = set(filter(None, app_config.get('permissions', "").split(',')))
        usb_devices: set[str] = set(filter(None, app_config.get('usb-devices', "").split(',')))
        for permission in permissions:
            permissionxml += permission_map[permission]
        if 'usb' in permissions:
            if not usb_devices:
                raise AndroidGeneratorException(f"Cannot find Usb-Devices in manifest. Found {app_config.keys()}")
            usb_intent_filter = '<action android:name="android.hardware.usb.action.USB_DEVICE_ATTACHED" />'
            usb_device_metadata = ('<meta-data android:name="android.hardware.usb.action.USB_DEVICE_ATTACHED" ' +
                                   'android:resource="@xml/device_filter" />')
            usb_device_filter_file_path = "app/src/main/res/xml/device_filter.xml"
            usb_device_filter_file_contents = '<?xml version="1.0" encoding="utf-8"?><resources>\n'
            for device_spec in usb_devices:
                vid, pid = device_spec.split(':')
                usb_device_filter_file_contents += f'<usb-device vendor-id="0x{vid}" product-id="0x{pid}"/>\n'
            usb_device_filter_file_contents += '</resources>\n'
            gen.GenerateFileWithContents(usb_device_filter_file_path, usb_device_filter_file_contents)
        if 'navigation' in permissions:
            navigation_svc = '<service android:name=".LocationForegroundService" android:foregroundServiceType="location" />'

        formatvars["permissionxml"] = permissionxml
        formatvars["usb_intent_filter"] = usb_intent_filter
        formatvars["usb_device_metadata"] = usb_device_metadata
        formatvars["navigation_svc"] = navigation_svc
        manifestcontent = manifesttemplate.format(**formatvars)
        gen.GenerateFileWithContents(manifestfilepath, manifestcontent)

    def Generate(self, *_args: Any):
        for name, configfile in self.GetAppManifests().items():
            bdir = self.android_workspace / name
            gen, config = self.GenerateBuildDirForManifest(name, configfile, bdir, bindings={
                "AndroidNDKPath": self.androidtools["ndk"].as_posix(),
                "AndroidNDKVersion": self.androidtools["ndk_version"],
                "AndroidSDKVersion": self.androidtools["sdk_version"]})
            self._GeneratorAppManifestXml(gen, config)
            self._Run(bdir, [externaltools.GetGradle().as_posix(), "wrapper"])

    def _Run(self, builddir: pathlib.Path, cmd: list[str], env: Optional[Dict[str, str]] = None, **kwargs: Any):
        runenv = (os.environ.copy() | self._env | (env or {}))
        cmakeexe = externaltools.GetCMAKE()
        runenv["PATH"] += os.pathsep + cmakeexe.parent.as_posix() + os.pathsep + externaltools.GetNinja().parent.as_posix()
        subprocess.check_call(cmd, cwd=str(builddir), env=runenv, **kwargs)

    def Build(self, *_args: Any):
        for name, _configfile in self.GetAppManifests().items():
            bdir = self.android_workspace / name
            gradlew = "gradlew.bat" if sys.platform == "win32" else "gradlew"
            self._Run(bdir, [(bdir / gradlew).as_posix(), "assembleRelease", "--info"])

    def Package(self, *_args: Any):
        for name, _configfile in self.GetAppManifests().items():
            bdir = self.android_workspace / name
            gradlew = "gradlew.bat" if sys.platform == "win32" else "gradlew"
            # Assemble => APK
            # Bundle => AAB
            self._Run(bdir, [(bdir / gradlew).as_posix(), "assembleRelease", "bundleRelease", "--info"])
            bundlefile = bdir / "app" / "build" / "outputs" / "bundle" / "release" / "app-release.aab"
            if not os.path.exists(bundlefile):
                raise AndroidGeneratorException(f"Cannot find {bundlefile}")
            storepass = configenv.ConfigEnv(str(self.GetSourceDir())).GetConfigStr('ANDROID_KEYSTORE_PASS')
            keystore = configenv.ConfigEnv(str(self.GetSourceDir())).GetConfigPath('ANDROID_KEYSTORE_FILE').as_posix()
            self._Run(bdir, [externaltools.GetJarSigner().as_posix(),
                             "-verbose", "-sigalg", "SHA256withRSA", "-digestalg", "SHA-256",
                             "-keystore", keystore, "-storepass", storepass, bundlefile.as_posix(), "key0"])

    def Clean(self, *_args: Any):
        if sys.platform == "linux":
            os.system("killall -15 java")
        elif sys.platform == "win32":
            os.system("taskkill /F /IM java.exe")
        else:
            pass
        subprocess.run(
            [str(shutil.which("adb", path=(externaltools.GetAndroidSdkRoot() / "platform-tools").as_posix())), "emu", "kill"], check=False)
        bdir = self.android_workspace
        for name in self.GetAppManifests():
            if os.path.isdir(bdir / name):
                self.rmtree(bdir / name)

    def Open(self, *_args: Any):
        for name in self.GetAppManifests():
            bdir = self.android_workspace / name
            self._Run(bdir, [externaltools.GetAndroidStudio().as_posix(), bdir.as_posix()], close_fds=True)


class AndroidCLIHandler(cmake.CMakeHandler):
    def __init__(self, srcdir: pathlib.Path, builddir: Optional[pathlib.Path] = None, **kargs):
        super(AndroidCLIHandler, self).__init__("andcli", srcdir, builddir, **kargs)
        self.ndkroot = externaltools.GetAndroidNDKRoot()
        self.toolchain = os.path.join(self.ndkroot, "build", "cmake", "android.toolchain.cmake")

    def GetGenerator(self):
        return "Ninja"

    def GetGeneratorArgs(self, arch: str, _config: str) -> list[str]:
        archmapping = {
            "arm": "armeabi-v7a",
            "arm64": "arm64-v8a"
        }
        # "-DANDROID_STL=c++_static", "-D__ANDROID_API__=21",
        return ["-DANDROID=1", "-DANDROID_NATIVE_API_LEVEL=28", "-DCMAKE_TOOLCHAIN_FILE=" +
                self.toolchain, "-DANDROID_ABI=" + archmapping[arch]]
