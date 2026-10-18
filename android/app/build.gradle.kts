plugins {
  id("com.android.application")
}

val toolchain =
  providers
    .exec {
      commandLine(
        providers.environmentVariable("CMAKE").getOrElse("cmake"),
        "-DBEAM_ANDROID_ACTION=properties",
        "-P",
        "${rootDir.parentFile}/cmake/AndroidTools.cmake",
      )
    }
    .standardOutput
    .asText
    .get()
    .lineSequence()
    .filter { it.contains('=') }
    .associate { it.substringBefore('=') to it.substringAfter('=') }
val platformVersion = toolchain.getValue("platform").split('.').map(String::toInt)

require(!project.hasProperty("beamAbis")) {
  "Beam Android supports only arm64-v8a; remove the obsolete beamAbis override"
}

android {
  namespace = "org.beam.android"
  compileSdk {
    version = release(platformVersion[0]) { minorApiLevel = platformVersion.getOrElse(1) { 0 } }
  }
  buildToolsVersion = toolchain.getValue("buildTools")
  ndkVersion = toolchain.getValue("ndkVersion")
  ndkPath = toolchain.getValue("ndk")
  defaultConfig {
    applicationId = "org.beam.android"
    minSdk = 29
    targetSdk = platformVersion[0]
    versionCode = 10
    versionName = "0.10.0"
    testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    ndk {
      abiFilters += "arm64-v8a"
    }
    externalNativeBuild {
      cmake {
        arguments += "-DBEAM_ANDROID_DEPS=${rootDir.parentFile}/build/android-deps"
        arguments += "-DANDROID_STL=c++_shared"
      }
    }
  }
  externalNativeBuild {
    cmake {
      path = file("../native/CMakeLists.txt")
      version = toolchain.getValue("cmake")
    }
  }
  compileOptions {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
  }
  sourceSets.getByName("main").assets.srcDir("../../third_party/licenses")
  lint {
    abortOnError = true
    warningsAsErrors = true
    // Beam intentionally ships ARM64 only; native Intel ChromeOS is out of scope.
    disable += "ChromeOsAbiSupport"
  }
}

dependencies {
  implementation("androidx.activity:activity:1.13.0")
  implementation("androidx.camera:camera-camera2:1.6.2")
  implementation("androidx.camera:camera-lifecycle:1.6.2")
  implementation("androidx.camera:camera-view:1.6.2")
  implementation("com.google.zxing:core:3.5.4")
  testImplementation("junit:junit:4.13.2")
  androidTestImplementation("androidx.test:runner:1.7.0")
  androidTestImplementation("junit:junit:4.13.2")
}
