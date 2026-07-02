# Build stage: compile the engine, run the C++ test suite, produce a wheel.
FROM python:3.12-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ cmake ninja-build git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B build -G Ninja -DLOB_BUILD_PYTHON=OFF \
    && cmake --build build \
    && ctest --test-dir build --output-on-failure

RUN pip wheel . --no-deps -w /wheels

# Runtime stage: just Python and the installed package.
FROM python:3.12-slim

COPY --from=build /wheels /wheels
RUN pip install --no-cache-dir /wheels/*.whl && rm -rf /wheels

CMD ["python", "-c", "import lob; e = lob.MatchingEngine(); e.submit(id=1, side=lob.Side.Sell, quantity=10, price=100); print('lob-engine', lob.__version__, '- best ask:', e.best_ask)"]
