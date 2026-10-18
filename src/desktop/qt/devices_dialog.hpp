#pragma once
#include "desktop/service.hpp"
#include <functional>
class QWidget;
class QLabel;
namespace beam::desktop {
void show_devices_dialog(QWidget* parent, const Config& config, Service& service,
                         const std::function<void()>& changed, QLabel* connectivity);
}
