FROM alpine:3.22

WORKDIR /app

COPY Bin/GFoundation /app/GFoundation
COPY Bin/TcpServerTest.so /app/TcpServerTest.so

EXPOSE 8888

ENTRYPOINT ["/app/GFoundation", "--modules=TcpServerTest"]
