FROM arm32v7/debian:bullseye

# Install dependencies and add the Rust toolchain
RUN apt-get update && apt-get install -y \
    build-essential \
    vim \
    curl \
    wget \
    build-essential \
    && rm -rf /var/lib/apt/lists/*

# Install Rust using rustup
RUN curl https://sh.rustup.rs -sSf | sh -s -- -y

# Add cargo to PATH
ENV PATH="/root/.cargo/bin:${PATH}"

WORKDIR /project

ENTRYPOINT ["/bin/bash"]
CMD ["/project/setup.sh"]
