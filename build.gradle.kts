plugins {
    java
    application
}

sourceSets {
    main {
        java {
            srcDir("src")
            exclude("test")
            exclude("strgen")
        }
        
    }
}

dependencies {
    implementation(
        files(
            "lib/java-getopt-1.0.13.jar",
            "lib/jfreechart-1.0.19.jar",
            "lib/jcommon-1.0.23.jar"
        )
    )
}

application {
    mainClass.set("game.Main")
}
