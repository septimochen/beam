plugins {
  id("com.android.application")
}

android {
  namespace = "org.beam.android"
  compileSdk = 37
  ndkVersion = "28.2.13676358"
  defaultConfig {
    applicationId = "org.beam.android"
    minSdk = 29
    targetSdk = 37
    versionCode = 10
    versionName = "0.10.0"
    testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    ndk {
      abiFilters += listOf("arm64-v8a", "x86_64")
      (project.findProperty("beamAbis") as String?)?.let {
        abiFilters.clear()
        abiFilters += it.split(",")
      }
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
      version = "3.31.6"
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
