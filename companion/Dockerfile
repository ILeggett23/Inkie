FROM python:3.12-slim
ENV PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1
WORKDIR /app
COPY requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt && useradd --uid 10001 --create-home app
COPY companion ./companion
COPY web ./web
RUN mkdir -p /var/lib/inkbound && chown app:app /var/lib/inkbound
USER 10001:10001
EXPOSE 8765
HEALTHCHECK --interval=30s --timeout=5s CMD python -c "import os,urllib.request; u=os.environ['INKBOUND_ORIGIN']; r=urllib.request.Request('http://127.0.0.1:8765/healthz',headers={'Host':u.split('://',1)[1]}); urllib.request.urlopen(r,timeout=4)"
CMD ["python", "-m", "companion.server", "--host", "0.0.0.0", "--port", "8765"]
