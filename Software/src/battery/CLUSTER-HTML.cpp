#include "CLUSTER-HTML.h"
#include <Arduino.h>
#include "CLUSTER-CAN.h"
#include "CLUSTER-PROTOCOL.h"

using namespace cluster_protocol;

String ClusterHtmlRenderer::get_status_html() {
  String html;
  html.reserve(2048);
  html += "<h4>Cluster pack status</h4>";
  html += "<table style='border-collapse:collapse'>";
  html += "<tr><th>Pack #</th><th>Status</th><th>V (V)</th><th>I (A)</th>"
          "<th>SOC %</th><th>T max (&deg;C)</th><th>Last seen (ms ago)</th></tr>";

  uint32_t now = millis();
  for (uint8_t pack_id = MIN_VALID_PACK_ID; pack_id <= MAX_VALID_PACK_ID; ++pack_id) {
    const PackSnapshot& s = battery.get_pack(pack_id);
    if (!s.seen_ever) continue;
    uint32_t age = now - s.last_seen_ms;
    html += "<tr><td>";
    html += pack_id;
    html += "</td><td>";
    html += s.alive ? "ALIVE" : "LOST";
    html += "</td><td>";
    html += s.voltage_dV / 10.0f;
    html += "</td><td>";
    html += s.current_dA / 10.0f;
    html += "</td><td>";
    html += s.reported_soc / 100.0f;
    html += "</td><td>";
    html += s.temperature_max_dC / 10.0f;
    html += "</td><td>";
    html += age;
    html += "</td></tr>";
  }
  html += "</table>";
  return html;
}
