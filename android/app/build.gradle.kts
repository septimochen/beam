import java.util.Properties

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

val unsignedRelease =
  providers.gradleProperty("beamUnsignedRelease").map(String::toBooleanStrict).getOrElse(false)
val releaseSigningFile =
  providers
    .environmentVariable("BEAM_ANDROID_SIGNING_PROPERTIES")
    .orNull
    ?.takeIf { it.isNotBlank() }
    ?.let(rootProject::file)
    ?: File(System.getProperty("user.home"), ".android/beam-release/signing.properties")
val releaseSigning =
  Properties().apply {
    if (!unsignedRelease && releaseSigningFile.isFile) {
      releaseSigningFile.reader(Charsets.UTF_8).use { load(it) }
    }
  }
val signingFields = listOf("storeFile", "storePassword", "keyAlias", "keyPassword")
val missingSigningFields = signingFields.filter { releaseSigning.getProperty(it).isNullOrBlank() }
val releaseKeystore =
  releaseSigningFile.parentFile.resolve(releaseSigning.getProperty("storeFile", ""))
val releaseSigningReady = !unsignedRelease && missingSigningFields.isEmpty()

val verifyReleaseSigning =
  tasks.register("verifyReleaseSigning") {
    doLast {
      if (!unsignedRelease) {
        check(releaseSigningFile.isFile) {
          "Release signing file is missing: $releaseSigningFile. " +
            "Set BEAM_ANDROID_SIGNING_PROPERTIES or follow docs/android.md#release-apk. " +
            "Use make android-release-unsigned for an explicitly unsigned build."
        }
        check(missingSigningFields.isEmpty()) {
          "Release signing file is missing fields: ${missingSigningFields.joinToString()}"
        }
        check(releaseKeystore.isFile) { "Release keystore is missing: $releaseKeystore" }
      }
    }
  }

tasks.matching { it.name == "preReleaseBuild" }.configureEach { dependsOn(verifyReleaseSigning) }

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
  signingConfigs {
    if (releaseSigningReady) {
      create("release") {
        storeFile = releaseKeystore
        storePassword = releaseSigning.getProperty("storePassword")
        keyAlias = releaseSigning.getProperty("keyAlias")
        keyPassword = releaseSigning.getProperty("keyPassword")
      }
    }
  }
  buildTypes {
    getByName("release") {
      isDebuggable = false
      isJniDebuggable = false
      isMinifyEnabled = true
      isShrinkResources = true
      proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
      externalNativeBuild { cmake { arguments += "-DCMAKE_BUILD_TYPE=Release" } }
      if (releaseSigningReady) {
        signingConfig = signingConfigs.getByName("release")
      }
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
