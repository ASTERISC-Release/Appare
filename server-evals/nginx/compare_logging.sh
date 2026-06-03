# Performing Requests without logging
echo -e 'No logging\n'
ssh seclog@localhost -p 8000 "cd module && make remove"
result1=$(./benchmark.sh | grep "Requests per second:")

# Performing Requests with Seclog
echo -e 'Secure logging\n'
ssh seclog@localhost -p 8000 "cd module && ./securelog.sh"
ssh seclog@localhost -p 8000 "cd eval/nginx && ./start-nginx-audit.sh"
result2=$(./benchmark.sh | grep "Requests per second:")

# Performing Requests with KAudit
echo -e 'KAudit logging\n' 
ssh seclog@localhost -p 8000 "cd module && ./kaudit.sh"
ssh seclog@localhost -p 8000 "cd eval/nginx && ./start-nginx-audit.sh"
result3=$(./benchmark.sh | grep "Requests per second:")

echo -e 'Results\n'
echo -e "No logging = $result1\n"
echo -e "Secure Logging = $result2\n"
echo -e "KAudit logging = $result3\n"