FROM python:3.12-slim
WORKDIR /app
COPY lobby_server.py .
ENV LOBBY_PORT=27020
EXPOSE 27020/udp
CMD ["sh", "-c", "python3 -u lobby_server.py --port ${LOBBY_PORT}"]
