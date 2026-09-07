# Security policy

Only the latest `main` development revision is maintained. There is no stable security-supported release yet. Preview binaries are unsigned.

Please report suspected vulnerabilities privately using [GitHub private vulnerability reporting](https://github.com/tang-vu/cantodeck/security/advisories/new). Include the affected revision, reproduction steps and impact. Do not attach credentials, private recordings or unnecessary personal identifiers. If private reporting is unavailable, open an issue asking for a private contact channel without disclosing exploit details.

The maintainer will review reports on a best-effort basis; no response-time SLA is promised. Coordinate public disclosure after assessment and remediation.

Core use requires no account, cloud service or telemetry. Microphone files are written only when recording is explicitly started; diagnostic probes do not save audio. CLI diagnostics include device display names, so inspect reports before sharing. Native WAV decoding and device input should be treated as untrusted data. A digital limiter does not protect against all acoustic feedback or unsafe loudness.
