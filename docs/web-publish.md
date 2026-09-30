# Publishing a base on IIS / Apache (reverse proxy)

The web backend (`wenterprise-server`) is a standalone HTTP server (cpp-httplib)
that serves a base at `http://<host>:<port>/w/<url>/`. It is **proxy-aware** — SSE
(`/stream`) sets `X-Accel-Buffering: no` and the client uses relative URLs — so the
supported way to publish a base under IIS or Apache is a **reverse proxy** in front
of the running backend. No separate transport, ISAPI module, or FastCGI host is
involved.

```
browser ──▶ IIS / Apache  ──(reverse proxy)──▶  127.0.0.1:<port>/w/<url>/  (wenterprise-server)
             (public site)                        (local backend, one per base)
```

## 1. Generate the front-end config

`wenterprise-server --publish=<iis|apache|both>` writes the config and exits — it
touches no database, so it works without a running base.

```
wenterprise-server --file=<base> --url=<name> --port=<N> ^
                   --publish=both --pubdir=<out> [--site-path=/<name>]
```

- `--url` / `--port` — must match how you run the serving backend (below); they set
  the proxy target `http://127.0.0.1:<port>/w/<url>/`.
- `--site-path` — the PUBLIC path on the IIS/Apache site (default `/<url>`).
- `--pubdir` — output directory (default: current).

Outputs:
- **`web.config`** — for IIS (URL Rewrite + ARR reverse-proxy rule).
- **`oes-<url>.conf`** — for Apache (`mod_proxy` `ProxyPass` / `ProxyPassReverse`).

## 2. Run the backend (kept alive)

The proxy forwards to a backend that must be listening locally:

```
wenterprise-server --file=<base> --url=<name> --port=<N> --host=127.0.0.1
```

For production, run it as a service so it restarts with the machine:
- **Windows**: `sc create OES-<name> binPath= "...\wenterprise-server.exe --file=... --url=<name> --port=<N> --host=127.0.0.1" start= auto` (or use NSSM).
- **Linux**: a systemd unit with `ExecStart=` the same command.

## 3. Wire up the front-end web server

### IIS
1. Install **URL Rewrite** and **Application Request Routing (ARR)**.
2. Enable the ARR proxy: IIS Manager → server node → *Application Request Routing
   Cache* → *Server Proxy Settings* → check **Enable proxy**.
3. For live updates (SSE `/stream`): in the same dialog set **Response buffer
   threshold (KB) = 0** so events flush immediately.
4. Create a Site or Application mapped to the folder holding the generated
   `web.config`, then browse it.

### Apache
1. Enable modules: `mod_proxy`, `mod_proxy_http` (e.g. `a2enmod proxy proxy_http`).
2. Include the generated `oes-<url>.conf` from a `<VirtualHost>` (or drop it in
   `conf.d/`), then reload Apache.

## Notes & limits
- **One backend process per base** (per `--url`/`--port`); publish several bases by
  running several backends on distinct ports and generating a config for each.
- The generated configs are a **starting point** — adjust host names, TLS, and the
  public path to your deployment. TLS terminates at IIS/Apache; the proxy hop to
  `127.0.0.1` stays plain HTTP on the loopback.
- A published base must, of course, **open** in the backend; if `wenterprise-server`
  fails to open the base, fix that first (the proxy only forwards).
- The native **FastCGI/ISAPI** hosting model (IIS/Apache spawn and manage the
  process, 1C-style) is not implemented — it would be a separate, larger increment.
