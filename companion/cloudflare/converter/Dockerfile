FROM python:3.12-slim-bookworm
ENV PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1
WORKDIR /app
COPY requirements.txt /app/requirements.txt
RUN pip install --no-cache-dir --requirement /app/requirements.txt && useradd --uid 10001 --create-home converter
COPY companion/convert.py /app/companion/convert.py
COPY companion/__init__.py /app/companion/__init__.py
COPY cloudflare/converter/server.py /app/server.py
USER 10001:10001
EXPOSE 8080
CMD ["python","/app/server.py"]
