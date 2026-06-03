#!/bin/bash -e

# Install NGINX in server
sudo apt update && sudo apt install nginx

# Create a 10KB file for testing purposes
sudo dd if=/dev/urandom of=/var/www/html/10K.html bs=10K count=1

echo "-----------------------------------------------------"
echo "Change the NGINX processes based on your requirements"
echo "          vim /etc/nginx/nginx.conf"
echo "-----------------------------------------------------"