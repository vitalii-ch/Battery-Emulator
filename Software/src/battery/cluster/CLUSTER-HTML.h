#ifndef CLUSTER_HTML_H
#define CLUSTER_HTML_H

#include "../../devboard/webserver/BatteryHtmlRenderer.h"

class ClusterCanBattery;

class ClusterHtmlRenderer : public BatteryHtmlRenderer {
 public:
  ClusterHtmlRenderer(const ClusterCanBattery& battery) : battery(battery) {}
  String get_status_html() override;

 private:
  const ClusterCanBattery& battery;
};

#endif
