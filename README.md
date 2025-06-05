# Patient Bed IoT Simulator
![Results on Grafana](https://github.com/nairrohit83/iotpatientbed/blob/main/grafana_dashboard_snapshot.png)

## Overview

This project simulates patient bed telemetry (heart rate, SpO2, bed inclination) and publishes data securely to AWS IoT Core using MQTT over TLS. The data is collected and visualized using InfluxDB, Telegraf, and Grafana on an AWS EC2 instance.

---

## Features

- Simulates realistic patient bed telemetry
- Publishes securely to AWS IoT Core (MQTT over TLS)
- Data pipeline: AWS IoT → Telegraf (MQTT Consumer) → InfluxDB → Grafana
- Example configs for Telegraf and Grafana dashboard included

---

## Prerequisites

- AWS account with IoT Core and EC2 access
- Ubuntu 22.04+ (tested on WSL and AWS EC2)
- C++17 compiler (g++)
- [vcpkg](https://github.com/microsoft/vcpkg) for dependencies
- InfluxDB, Telegraf, Grafana

---

## 1. AWS IoT Core Setup

### a. Create IoT Things

1. Go to **AWS Console → IoT Core → Manage → Things → Create things**.
2. Choose **Create a single thing**.
3. Name your thing (e.g., `PatientBed1`). Repeat for `PatientBed2`.

### b. Create and Attach Certificates

1. During thing creation, choose **Create certificate**.
2. Download:
    - Device certificate (`*.pem.crt`)
    - Private key (`*.private.key`)
    - Amazon Root CA (`AmazonRootCA1.pem`)
3. Attach the certificate to the thing.
4. Activate the certificate.

### c. Create and Attach IoT Policy

1. Go to **Secure → Policies → Create policy**.
2. Example policy (allows connect, publish, subscribe, receive on `PatientBed/*`):

    ```json
    {
      "Version": "2012-10-17",
      "Statement": [
        {
          "Effect": "Allow",
          "Action": [
            "iot:Connect",
            "iot:Publish",
            "iot:Subscribe",
            "iot:Receive"
          ],
          "Resource": [
            "arn:aws:iot:<region>:<account-id>:topic/PatientBed/*",
            "arn:aws:iot:<region>:<account-id>:client/PatientBed*"
          ]
        }
      ]
    }
    ```
   - Replace `<region>` and `<account-id>` with your values.
   - You may use `"Resource": "*"` for testing, but restrict in production.

3. Attach the policy to the certificate.

### d. Note Your AWS IoT Endpoint

- Find it under **Settings** in the IoT Core console (e.g., `a22bv8r2s2kek2-ats.iot.<region>.amazonaws.com`).

---

## 2. Building and Running the Simulator

### a. Clone and Prepare

```sh
git clone https://github.com/yourusername/patientbedsimulator.git
cd patientbedsimulator
```

### b. Install Dependencies

```sh
sudo apt-get update
sudo apt-get install build-essential cmake git
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg && ./bootstrap-vcpkg.sh && cd ..
./vcpkg/vcpkg install paho-mqttpp3 nlohmann-json
```

### c. Build

```sh
g++ src/patientbedsimulation.cpp -o patientbedsimulation \
  -I./vcpkg/installed/x64-linux/include \
  -L./vcpkg/installed/x64-linux/lib \
  -lpaho-mqttpp3 -lpaho-mqtt3as -lssl -lcrypto -lpthread
```

### d. Run

```sh
./patientbedsimulation 1
./patientbedsimulation 2
```

- Place the correct certs in `certs/` as described above.

---

## 3. EC2 Instance Setup (Ubuntu) for InfluxDB, Telegraf, Grafana

### a. Launch EC2 Instance

- Ubuntu 22.04 LTS, t2.medium or better
- Open ports: 22 (SSH), 8086 (InfluxDB), 3000 (Grafana)

### b. Install InfluxDB

```sh
wget -qO- https://repos.influxdata.com/influxdb.key | sudo gpg --dearmor -o /usr/share/keyrings/influxdb-archive-keyring.gpg
echo 'deb [signed-by=/usr/share/keyrings/influxdb-archive-keyring.gpg] https://repos.influxdata.com/ubuntu jammy stable' | sudo tee /etc/apt/sources.list.d/influxdb.list
sudo apt-get update && sudo apt-get install influxdb2
sudo systemctl enable --now influxdb
```

- Access InfluxDB UI at `http://<EC2-IP>:8086` and create a bucket (e.g., `PatientBedData`).

### c. Install Telegraf

```sh
sudo apt-get install telegraf
```

- Copy `telegraf.conf` from this repo to `/etc/telegraf/telegraf.conf`.
- Place AWS IoT Core certs for Telegraf in a secure location on the EC2 instance.
- Edit the MQTT consumer section to use the correct cert paths and topics.

### d. Create AWS IoT Thing for Telegraf

- Create a new IoT Thing (e.g., `TelegrafCollector`).
- Generate and download a certificate and keys.
- Attach a policy allowing subscribe/receive on `PatientBed/#`:

    ```json
    {
      "Version": "2012-10-17",
      "Statement": [
        {
          "Effect": "Allow",
          "Action": [
            "iot:Connect",
            "iot:Subscribe",
            "iot:Receive"
          ],
          "Resource": [
            "arn:aws:iot:<region>:<account-id>:topic/PatientBed/*",
            "arn:aws:iot:<region>:<account-id>:client/TelegrafCollector"
          ]
        }
      ]
    }
    ```

- Update `telegraf.conf` with these certs.

### e. Start Telegraf

```sh
sudo systemctl restart telegraf
```

---

## 4. Install and Configure Grafana

```sh
sudo apt-get install -y software-properties-common
sudo add-apt-repository "deb https://packages.grafana.com/oss/deb stable main"
wget -q -O - https://packages.grafana.com/gpg.key | sudo apt-key add -
sudo apt-get update
sudo apt-get install grafana
sudo systemctl enable --now grafana-server
```

- Access Grafana at `http://<EC2-IP>:3000` (default admin/admin).
- Add InfluxDB as a data source (URL: `http://localhost:8086`, use your bucket and token).
- Import `grafanadashboardsettings.json` as a dashboard.

---

## 5. Security and Best Practices

- **Never commit real certificates or private keys to git.**
- Use `.gitignore` to exclude `certs/` and other sensitive files.
- Change default passwords and restrict security groups/firewall rules.

---

## 6. Troubleshooting

- Check logs for Telegraf, InfluxDB, and Grafana if data is missing.
- Use `mosquitto_sub` to debug MQTT topics if needed.
- Ensure time synchronization on all machines.

---

## 7. License

See [LICENSE](LICENSE).

---

## 8. VSCode Tasks

Your `.vscode/tasks.json` should include all necessary include and library paths for vcpkg, and link with `-lpaho-mqttpp3 -lpaho-mqtt3as -lssl -lcrypto -lpthread`.  
If you use the provided example, you are covered.

---

## 9. .gitignore Example

```
certs/
*.o
patientbedsimulation
.vscode/
build/
```

---

## 10. Credits

Author: Rohit Nair

---
