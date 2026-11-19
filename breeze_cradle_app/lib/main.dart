import 'dart:async';
import 'dart:convert';

import 'package:breeze_cradle_app/alert_service.dart';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;

class BreezePalette {
  static const canvas = Color(0xfff5f5f0);
  static const surface = Color(0xffffffff);
  static const ink = Color(0xff27352e);
  static const muted = Color(0xff738078);
  static const accent = Color(0xff557562);
  static const accentSoft = Color(0xffe7eee8);
  static const border = Color(0xffe3e7e1);
  static const warning = Color(0xffa36b35);
  static const warningSoft = Color(0xfff5eadb);
  static const danger = Color(0xffa9534b);
  static const dangerSoft = Color(0xfff6e8e5);
  static const blue = Color(0xff617f8a);
  static const blueSoft = Color(0xffe8eff0);
}

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  try {
    await AlertService.initialize();
  } catch (_) {
    // Keep the dashboard available if notification setup fails.
  }
  runApp(const BreezeCradleApp());
}

class BreezeCradleApp extends StatelessWidget {
  const BreezeCradleApp({super.key});

  @override
  Widget build(BuildContext context) => MaterialApp(
        title: 'Breeze Cradle',
        debugShowCheckedModeBanner: false,
        theme: ThemeData(
          useMaterial3: true,
          brightness: Brightness.light,
          scaffoldBackgroundColor: BreezePalette.canvas,
          colorScheme: ColorScheme.fromSeed(
            seedColor: BreezePalette.accent,
            brightness: Brightness.light,
            surface: BreezePalette.surface,
          ),
          appBarTheme: const AppBarTheme(
            backgroundColor: BreezePalette.canvas,
            foregroundColor: BreezePalette.ink,
          ),
          fontFamily: 'Roboto',
        ),
        home: const DashboardPage(),
      );
}

class CradleStatus {
  CradleStatus(this.json);
  final Map<String, dynamic> json;

  bool get dhtValid => json['dhtValid'] == true;
  double? get temperature => _number(json['temperature']);
  double? get humidity => _number(json['humidity']);
  double get distance => _number(json['distance']) ?? -1;
  bool get rocking => json['rocking'] == true;
  bool get babyMoving => json['babyMoving'] == true;
  int get angle => (json['angle'] as num?)?.toInt() ?? 90;
  int get moisture => (json['moisture'] as num?)?.toInt() ?? 0;
  bool get wet => json['wet'] == true;
  int get uptime => (json['uptime'] as num?)?.toInt() ?? 0;
  bool get fanOn => json['fanOn'] == true;
  bool get fanRecommended => json['fanRecommended'] == true;

  static double? _number(dynamic value) =>
      value is num ? value.toDouble() : null;
}

class DashboardPage extends StatefulWidget {
  const DashboardPage({super.key});

  @override
  State<DashboardPage> createState() => _DashboardPageState();
}

class _DashboardPageState extends State<DashboardPage> {
  static const double _temperatureAlertC = 30.0;
  static const int _moistureAlertPercent = 60;
  static const Duration _movementAlertCooldown = Duration(minutes: 1);
  final _hostController = TextEditingController(text: '192.168.4.1');
  CradleStatus? _status;
  Timer? _timer;
  bool _loading = true;
  String? _error;
  DateTime? _updated;
  bool? _wasHot;
  bool? _wasWet;
  bool _wasMoving = false;
  DateTime? _lastMovementAlert;

  @override
  void initState() {
    super.initState();
    _refresh();
    _timer = Timer.periodic(const Duration(seconds: 2), (_) => _refresh());
  }

  @override
  void dispose() {
    _timer?.cancel();
    _hostController.dispose();
    super.dispose();
  }

  Uri get _endpoint {
    var host = _hostController.text.trim();
    if (host.startsWith('http://')) host = host.substring(7);
    if (host.endsWith('/')) host = host.substring(0, host.length - 1);
    return Uri.parse('http://$host/api/status');
  }

  Future<void> _refresh() async {
    try {
      final response =
          await http.get(_endpoint).timeout(const Duration(seconds: 4));
      if (response.statusCode != 200) {
        throw Exception('HTTP ${response.statusCode}');
      }
      final decoded = jsonDecode(response.body) as Map<String, dynamic>;
      final status = CradleStatus(decoded);
      await _handleAlerts(status);
      if (!mounted) return;
      setState(() {
        _status = status;
        _updated = DateTime.now();
        _error = null;
        _loading = false;
      });
    } catch (_) {
      if (!mounted) return;
      setState(() {
        _error =
            'Can’t reach the ESP32. Connect to SmartCradle-Breeze and check the address.';
        _loading = false;
      });
    }
  }

  Future<void> _handleAlerts(CradleStatus status) async {
    final hot = status.dhtValid &&
        (status.temperature ?? double.negativeInfinity) >= _temperatureAlertC;
    if (hot && _wasHot != true) {
      await AlertService.show(
        id: 100,
        title: 'High temperature',
        body:
            'Room temperature is ${status.temperature!.toStringAsFixed(1)} °C (alert at ${_temperatureAlertC.toStringAsFixed(0)} °C).',
      );
    }
    _wasHot = hot;

    final wet = status.moisture >= _moistureAlertPercent;
    if (wet && _wasWet != true) {
      await AlertService.show(
        id: 101,
        title: 'Moisture detected',
        body:
            'Mattress moisture is ${status.moisture}% (alert at $_moistureAlertPercent%).',
      );
    }
    _wasWet = wet;

    final now = DateTime.now();
    final canNotifyMovement = _lastMovementAlert == null ||
        now.difference(_lastMovementAlert!) >= _movementAlertCooldown;
    if (status.babyMoving && !_wasMoving && canNotifyMovement) {
      await AlertService.show(
        id: 102,
        title: 'Movement detected',
        body: 'Ultrasonic sensor detected close-range movement.',
      );
      _lastMovementAlert = now;
    }
    _wasMoving = status.babyMoving;
  }

  @override
  Widget build(BuildContext context) {
    final s = _status;
    return Scaffold(
      body: SafeArea(
        child: RefreshIndicator(
          onRefresh: _refresh,
          color: BreezePalette.accent,
          child: CustomScrollView(
            physics: const AlwaysScrollableScrollPhysics(),
            slivers: [
              SliverPadding(
                padding: const EdgeInsets.fromLTRB(20, 18, 20, 30),
                sliver: SliverList.list(
                  children: [
                    _header(),
                    const SizedBox(height: 24),
                    _climateCard(s),
                    const SizedBox(height: 26),
                    _sectionTitle('Around the cradle', 'Live readings'),
                    const SizedBox(height: 12),
                    GridView.count(
                      crossAxisCount: 2,
                      shrinkWrap: true,
                      physics: const NeverScrollableScrollPhysics(),
                      mainAxisSpacing: 11,
                      crossAxisSpacing: 11,
                      childAspectRatio: 1.17,
                      children: [
                        _metricCard(
                          'Presence',
                          Icons.child_care_outlined,
                          s == null ? '—' : _presence(s),
                          s == null
                              ? 'Waiting for a reading'
                              : _distanceText(s),
                          s != null && s.distance >= 0 && s.distance <= 15
                              ? BreezePalette.accent
                              : BreezePalette.warning,
                        ),
                        _metricCard(
                          'Cradle',
                          Icons.sync_alt_rounded,
                          s == null
                              ? '—'
                              : s.rocking
                                  ? 'Rocking'
                                  : 'Still',
                          s == null ? 'Servo angle —' : 'Servo at ${s.angle}°',
                          BreezePalette.accent,
                        ),
                        _metricCard(
                          'Mattress',
                          Icons.water_drop_outlined,
                          s == null
                              ? '—'
                              : s.wet
                                  ? 'Wet'
                                  : 'Dry',
                          s == null ? 'Moisture —' : '${s.moisture}% moisture',
                          s?.wet == true
                              ? BreezePalette.danger
                              : BreezePalette.accent,
                        ),
                        _metricCard(
                          'Fan switch',
                          Icons.air_outlined,
                          s == null
                              ? '—'
                              : s.fanOn
                                  ? 'On'
                                  : 'Off',
                          s == null
                              ? 'Waiting for a reading'
                              : s.fanRecommended
                                  ? 'Temperature suggests on'
                                  : 'No cooling needed',
                          s?.fanOn == true
                              ? BreezePalette.accent
                              : BreezePalette.muted,
                        ),
                      ],
                    ),
                    const SizedBox(height: 27),
                    _sectionTitle('Connection', 'ESP32'),
                    const SizedBox(height: 12),
                    _connectionCard(),
                    const SizedBox(height: 22),
                    _noteCard(),
                  ],
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _header() => Row(
        children: [
          Container(
            width: 48,
            height: 48,
            decoration: BoxDecoration(
              color: BreezePalette.accentSoft,
              borderRadius: BorderRadius.circular(16),
            ),
            child: const Icon(Icons.air_rounded, color: BreezePalette.accent),
          ),
          const SizedBox(width: 12),
          const Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  'Breeze',
                  style: TextStyle(
                    color: BreezePalette.muted,
                    fontSize: 12,
                    fontWeight: FontWeight.w500,
                  ),
                ),
                SizedBox(height: 2),
                Text(
                  'Cradle monitor',
                  style: TextStyle(
                    color: BreezePalette.ink,
                    fontSize: 20,
                    fontWeight: FontWeight.w600,
                    letterSpacing: -0.3,
                  ),
                ),
              ],
            ),
          ),
          _connectionPill,
        ],
      );

  Widget get _connectionPill {
    final connected = _error == null && _status != null;
    final color = connected ? BreezePalette.accent : BreezePalette.warning;
    final background =
        connected ? BreezePalette.accentSoft : BreezePalette.warningSoft;
    final label = _loading && _status == null
        ? 'Connecting'
        : connected
            ? 'Connected'
            : 'Offline';
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 11, vertical: 8),
      decoration: BoxDecoration(
        color: background,
        borderRadius: BorderRadius.circular(30),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Container(
            width: 7,
            height: 7,
            decoration: BoxDecoration(color: color, shape: BoxShape.circle),
          ),
          const SizedBox(width: 6),
          Text(
            label,
            style: TextStyle(
              color: color,
              fontSize: 11,
              fontWeight: FontWeight.w600,
            ),
          ),
        ],
      ),
    );
  }

  BoxDecoration _cardDecoration({Color color = BreezePalette.surface}) =>
      BoxDecoration(
        color: color,
        borderRadius: BorderRadius.circular(22),
        border: Border.all(color: BreezePalette.border),
        boxShadow: const [
          BoxShadow(
            color: Color(0x0a27352e),
            blurRadius: 14,
            offset: Offset(0, 5),
          ),
        ],
      );

  Widget _climateCard(CradleStatus? s) => Container(
        padding: const EdgeInsets.all(19),
        decoration: _cardDecoration(),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Expanded(
                  child: Text(
                    'Room conditions',
                    style: TextStyle(
                      color: BreezePalette.ink,
                      fontSize: 17,
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                ),
                if (s?.dhtValid == true)
                  const Icon(Icons.check_circle,
                      color: BreezePalette.accent, size: 18),
              ],
            ),
            const SizedBox(height: 4),
            Text(
              _updated == null
                  ? 'Waiting for a reading'
                  : 'Updated ${TimeOfDay.fromDateTime(_updated!).format(context)}',
              style: const TextStyle(color: BreezePalette.muted, fontSize: 12),
            ),
            const SizedBox(height: 20),
            Row(
              children: [
                Expanded(
                  child: _climateValue(
                    Icons.thermostat_outlined,
                    s?.dhtValid == true
                        ? s!.temperature?.toStringAsFixed(1) ?? '—'
                        : '—',
                    '°C',
                    'Temperature',
                    BreezePalette.accent,
                  ),
                ),
                Container(width: 1, height: 68, color: BreezePalette.border),
                Expanded(
                  child: Padding(
                    padding: const EdgeInsets.only(left: 20),
                    child: _climateValue(
                      Icons.water_drop_outlined,
                      s?.dhtValid == true
                          ? s!.humidity?.round().toString() ?? '—'
                          : '—',
                      '%',
                      'Humidity',
                      BreezePalette.blue,
                    ),
                  ),
                ),
              ],
            ),
          ],
        ),
      );

  Widget _climateValue(
    IconData icon,
    String value,
    String unit,
    String label,
    Color color,
  ) =>
      Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(icon, color: color, size: 19),
          const SizedBox(height: 7),
          RichText(
            text: TextSpan(
              style: const TextStyle(
                fontFamily: 'Roboto',
                color: BreezePalette.ink,
              ),
              children: [
                TextSpan(
                  text: value,
                  style: const TextStyle(
                    fontSize: 34,
                    fontWeight: FontWeight.w600,
                    letterSpacing: -1,
                  ),
                ),
                TextSpan(
                  text: ' $unit',
                  style: const TextStyle(
                    fontSize: 14,
                    color: BreezePalette.muted,
                  ),
                ),
              ],
            ),
          ),
          Text(
            label,
            style: const TextStyle(color: BreezePalette.muted, fontSize: 12),
          ),
        ],
      );

  Widget _metricCard(
    String label,
    IconData icon,
    String value,
    String detail,
    Color valueColor,
  ) =>
      Container(
        padding: const EdgeInsets.all(14),
        decoration: _cardDecoration(),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Container(
                  width: 34,
                  height: 34,
                  decoration: BoxDecoration(
                    color: BreezePalette.accentSoft,
                    borderRadius: BorderRadius.circular(12),
                  ),
                  child: Icon(icon, size: 18, color: BreezePalette.accent),
                ),
                const SizedBox(width: 9),
                Expanded(
                  child: Text(
                    label,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(
                      color: BreezePalette.muted,
                      fontSize: 12,
                      fontWeight: FontWeight.w500,
                    ),
                  ),
                ),
              ],
            ),
            const Spacer(),
            FittedBox(
              alignment: Alignment.centerLeft,
              fit: BoxFit.scaleDown,
              child: Text(
                value,
                style: TextStyle(
                  color: valueColor,
                  fontSize: 20,
                  fontWeight: FontWeight.w600,
                ),
              ),
            ),
            const SizedBox(height: 4),
            Text(
              detail,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: const TextStyle(
                color: BreezePalette.muted,
                fontSize: 11,
              ),
            ),
          ],
        ),
      );

  Widget _sectionTitle(String title, String trailing) => Row(
        children: [
          Expanded(
            child: Text(
              title,
              style: const TextStyle(
                color: BreezePalette.ink,
                fontSize: 16,
                fontWeight: FontWeight.w600,
              ),
            ),
          ),
          Text(
            trailing,
            style: const TextStyle(color: BreezePalette.muted, fontSize: 12),
          ),
        ],
      );

  Widget _connectionCard() => Container(
        padding: const EdgeInsets.all(16),
        decoration: _cardDecoration(),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            TextField(
              controller: _hostController,
              keyboardType: TextInputType.url,
              style: const TextStyle(
                color: BreezePalette.ink,
                fontSize: 14,
              ),
              decoration: InputDecoration(
                labelText: 'Device address',
                hintText: '192.168.4.1',
                labelStyle: const TextStyle(color: BreezePalette.muted),
                prefixIcon: const Icon(
                  Icons.router_outlined,
                  size: 19,
                  color: BreezePalette.accent,
                ),
                filled: true,
                fillColor: BreezePalette.canvas,
                contentPadding:
                    const EdgeInsets.symmetric(vertical: 14, horizontal: 12),
                border: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(14),
                  borderSide: BorderSide.none,
                ),
                enabledBorder: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(14),
                  borderSide: BorderSide.none,
                ),
                suffixIcon: IconButton.filledTonal(
                  tooltip: 'Refresh connection',
                  onPressed: _refresh,
                  icon: const Icon(Icons.refresh_rounded),
                ),
              ),
              onSubmitted: (_) => _refresh(),
            ),
            const SizedBox(height: 14),
            const Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Padding(
                  padding: EdgeInsets.only(top: 1),
                  child: Icon(
                    Icons.wifi_rounded,
                    size: 17,
                    color: BreezePalette.accent,
                  ),
                ),
                SizedBox(width: 9),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        'SmartCradle-Breeze',
                        style: TextStyle(
                          color: BreezePalette.ink,
                          fontSize: 13,
                          fontWeight: FontWeight.w500,
                        ),
                      ),
                      SizedBox(height: 3),
                      Text(
                        'Join this Wi-Fi network on your phone to connect.',
                        style: TextStyle(
                          color: BreezePalette.muted,
                          fontSize: 11,
                        ),
                      ),
                    ],
                  ),
                ),
              ],
            ),
            if (_error != null) ...[
              const SizedBox(height: 12),
              Container(
                padding: const EdgeInsets.all(11),
                decoration: BoxDecoration(
                  color: BreezePalette.warningSoft,
                  borderRadius: BorderRadius.circular(12),
                ),
                child: Row(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const Icon(Icons.info_outline_rounded,
                        color: BreezePalette.warning, size: 17),
                    const SizedBox(width: 8),
                    Expanded(
                      child: Text(
                        _error!,
                        style: const TextStyle(
                          color: BreezePalette.ink,
                          fontSize: 12,
                          height: 1.35,
                        ),
                      ),
                    ),
                  ],
                ),
              ),
            ],
            if (_updated != null) ...[
              const SizedBox(height: 12),
              Text(
                'Last update ${TimeOfDay.fromDateTime(_updated!).format(context)}  ·  Uptime ${_duration(_status?.uptime ?? 0)}',
                style:
                    const TextStyle(color: BreezePalette.muted, fontSize: 11),
              ),
            ],
          ],
        ),
      );

  Widget _noteCard() => Container(
        padding: const EdgeInsets.fromLTRB(15, 14, 15, 15),
        decoration: _cardDecoration(color: BreezePalette.accentSoft),
        child: const Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Icon(Icons.notifications_none_rounded,
                color: BreezePalette.accent, size: 19),
            SizedBox(width: 10),
            Expanded(
              child: Text(
                'Alerts start at 30 °C, 60% moisture, or close-range movement. Keep the app open and connected for live updates. This is a project monitor, not a medical or safety device.',
                style: TextStyle(
                  color: BreezePalette.ink,
                  fontSize: 11,
                  height: 1.4,
                ),
              ),
            ),
          ],
        ),
      );

  String _presence(CradleStatus s) => s.distance < 0
      ? 'No echo'
      : s.distance <= 4
          ? 'Moving'
          : s.distance <= 15
              ? 'Present'
              : 'No presence';
  String _distanceText(CradleStatus s) => s.distance < 0
      ? 'Check ultrasonic sensor'
      : '${s.distance.toStringAsFixed(1)} cm away';
  String _duration(int seconds) {
    final days = seconds ~/ 86400;
    final hours = (seconds % 86400) ~/ 3600;
    final mins = (seconds % 3600) ~/ 60;
    return days > 0 ? '${days}d ${hours}h' : '${hours}h ${mins}m';
  }
}
