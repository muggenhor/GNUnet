echo "Generating CA"

openssl req -new -x509 -days 3650 -extensions v3_ca -keyout gnscakey.pem -out gnscacert.pem -subj "/C=DE/ST=Bavaria/L=Munich/O=TUM/OU=IN/CN=GNS Proxy CA/emailAddress=bounce@gnunet.org" -passout pass:"GNUnet Naming System"

echo "Removing passphrase from key"
openssl rsa -passin pass:"GNUnet Naming System" -in gnscakey.pem -out gnscakeynoenc.pem

cp gnscacert.pem $HOME/.gnunet/gns/gnscert.pem
cat gnscacert.pem >> $HOME/.gnunet/gns/gnsCAcert.pem
cat gnscakeynoenc.pem >> $HOME/.gnunet/gns/gnsCAcert.pem
cat gnscakey.pem
cat gnscacert.pem

echo "Cleaning up"
rm gnscakey.pem gnscakeynoenc.pem gnscacert.pem

echo "Next steps:"
echo "1. The new CA will be used automatically by the proxy with the default settings"
echo "2. Please import the certificate $HOME/.gnunet/gns/gnscert.pem into the browser of your choice"
echo "3. Start gnunet-gns-proxy and configure your broser to use a SOCKS proxy on port 7777"
