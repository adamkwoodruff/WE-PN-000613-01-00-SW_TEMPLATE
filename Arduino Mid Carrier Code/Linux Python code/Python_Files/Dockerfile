FROM python:alpine

# Set environment variables
ENV DEBIAN_FRONTEND=noninteractive
ENV PYTHONUNBUFFERED=1

# Set working directory inside the container
WORKDIR /app

# Copy project files
COPY requirements.txt .
COPY portenta_linux_bridge.py .
COPY config.json .
# Install dependencies
RUN pip install --no-cache-dir -r requirements.txt

# Expose RPC port (optional)
EXPOSE 5005
EXPOSE 5001
# Command to run the script
CMD ["python", "portenta_linux_bridge.py"]
