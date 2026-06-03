# Testing NGINX using ApacheBench

- On server, execute `prepare-server.sh` to install NGINX
    - NGINX should start automatically, if you would like to start it again,
      execute `start-nginx.sh`

- On client, execute `sudo apt install apache2-utils` to install ApacheBench.

## NGINX with auditing

Run `start-nginx-auditd.sh`, which will find the process IDs for NGINX and
automatically begin logging them. If you want to stop logging, run `stop-audit.sh`.

## Troubleshooting

If the requests are mpt getting logged go to /etc/nginx/nginx.conf and change worker_processes from auto to 2.
This will ensure that PIDs for worker threads are allocated incrementally.
    You  need to restart the nginx service using `sudo service nginx restart`

