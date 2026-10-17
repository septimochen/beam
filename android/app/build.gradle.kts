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
    versionCode = 9
    versionName = "0.9.0"
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
  lint {
    abortOnError = true
    warningsAsErrors = true
  }
}

dependencies {
  testImplementation("junit:junit:4.13.2")
  androidTestImplementation("androidx.test:runner:1.7.0")
  androidTestImplementation("junit:junit:4.13.2")
}
