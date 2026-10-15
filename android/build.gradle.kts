plugins {
  id("com.android.application") version "9.2.0" apply false
}

val formatter by configurations.creating {
  attributes {
    attribute(Bundling.BUNDLING_ATTRIBUTE, objects.named(Bundling.SHADOWED))
    attribute(Usage.USAGE_ATTRIBUTE, objects.named(Usage.JAVA_RUNTIME))
    attribute(
      TargetJvmEnvironment.TARGET_JVM_ENVIRONMENT_ATTRIBUTE,
      objects.named(TargetJvmEnvironment.STANDARD_JVM),
    )
  }
}

dependencies { formatter("com.facebook:ktfmt:0.64") }

val kotlinSources =
  fileTree(rootDir) {
    include("**/*.kt", "**/*.kts")
    exclude("**/build/**", "**/.gradle/**", "**/.cxx/**")
  }

for (check in listOf(false, true)) {
  tasks.register<JavaExec>(if (check) "formatCheck" else "formatKotlin") {
    classpath = formatter
    mainClass.set("com.facebook.ktfmt.cli.Main")
    args("--google-style")
    if (check) args("--dry-run", "--set-exit-if-changed")
    args(kotlinSources.files.sorted().map { it.path })
  }
}
