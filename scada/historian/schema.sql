CREATE TABLE IF NOT EXISTS telemetry (
  ts INTEGER, vehicle_id INTEGER, lat REAL, lon REAL, speed_kmh REAL, heading REAL,
  accel REAL, roll INTEGER, pitch INTEGER, flags INTEGER, battery INTEGER,
  rssi INTEGER, pdr REAL, distance REAL, ttc REAL, warn INTEGER);
CREATE TABLE IF NOT EXISTS alarms (
  id INTEGER PRIMARY KEY AUTOINCREMENT, ts INTEGER, vehicle_id INTEGER,
  type TEXT, severity TEXT, lat REAL, lon REAL,
  acked INTEGER DEFAULT 0, ack_ts INTEGER, cleared_ts INTEGER);
