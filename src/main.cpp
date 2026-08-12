#include <Arduino.h>

#include "core/Application.h"

namespace {
variometer::Application app;
}

void setup() {
    app.begin();
}

void loop() {
    app.loop();
}
