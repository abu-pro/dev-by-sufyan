plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "com.sufyan.devbysufyan"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.sufyan.devbysufyan"
        minSdk = 26
        targetSdk = 35
        versionCode = 2
        versionName = "1.2.0"
    }

    // The GitHub Actions workflow generates a throwaway keystore in the
    // runner and passes its location/passwords through these env vars.
    val dbsKeystore = System.getenv("DBS_KEYSTORE")
    if (dbsKeystore != null) {
        signingConfigs {
            create("release") {
                storeFile = file(dbsKeystore)
                storePassword = System.getenv("DBS_KEYSTORE_PASS")
                keyAlias = System.getenv("DBS_KEY_ALIAS")
                keyPassword = System.getenv("DBS_KEY_PASS")
            }
        }
    }
    buildTypes {
        release {
            isMinifyEnabled = false
            if (dbsKeystore != null) {
                signingConfig = signingConfigs.getByName("release")
            }
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
    buildFeatures {
        compose = true
    }
}

dependencies {
    val composeBom = platform("androidx.compose:compose-bom:2024.09.03")
    implementation(composeBom)
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-graphics")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-extended")
    implementation("com.squareup.okhttp3:okhttp:4.12.0")
}
