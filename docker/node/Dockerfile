FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    build-essential g++ cmake \
    libssl-dev \
    libboost-dev \
    wget bison flex perl python3 \
    qtbase5-dev libqt5opengl5-dev libxml2-dev zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

# Install OMNeT++ (headless)
WORKDIR /opt
RUN wget -q https://github.com/omnetpp/omnetpp/releases/download/omnetpp-6.0.3/omnetpp-6.0.3-linux-x86_64.tgz \
    && tar xfz omnetpp-6.0.3-linux-x86_64.tgz \
    && cd omnetpp-6.0.3 \
    && . setenv \
    && ./configure WITH_QTENV=no WITH_OSG=no \
    && make -j$(nproc) MODE=release \
    && rm /opt/omnetpp-6.0.3-linux-x86_64.tgz

ENV PATH="/opt/omnetpp-6.0.3/bin:${PATH}"

COPY . /charon
WORKDIR /charon
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)

CMD ["./build/charon_sim", \
     "-u", "Cmdenv", \
     "-f", "simulations/scenarios/omnetpp.ini", \
     "-c", "ER_baseline"]
