#pragma once
#include "desktop/service.hpp"
class QWidget;
namespace beam::desktop {
void show_pairing_dialog(QWidget* parent, const Config& config, Service& service);
}
