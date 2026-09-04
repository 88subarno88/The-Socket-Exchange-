# ============================================================================
# Makefile  --  builds the exchange server, both clients, and the bonus tool
# into ./bin/ (which the run-* launcher scripts expect).
#
# On FreeBSD, the default `make` is BSD make, NOT GNU make. This Makefile is
# written to work with BOTH. If something odd happens, try `gmake` (pkg install
# gmake) -- but plain `make` should be fine here.
#
# Build:   make            (or  gmake)
# Clean:   make clean
# ============================================================================

CXX      ?= c++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra
BIN       = bin

COMMON    = src/common/protocol.cpp src/common/net_utils.cpp

all: $(BIN)/exchange_server $(BIN)/trader_client $(BIN)/market_data_client $(BIN)/conn_generator

$(BIN):
	mkdir -p $(BIN)

$(BIN)/exchange_server: $(BIN) src/server/exchange_server.cpp src/server/order_book.cpp $(COMMON)
	$(CXX) $(CXXFLAGS) -o $@ src/server/exchange_server.cpp src/server/order_book.cpp $(COMMON)

$(BIN)/trader_client: $(BIN) src/trader/trader_client.cpp $(COMMON)
	$(CXX) $(CXXFLAGS) -o $@ src/trader/trader_client.cpp $(COMMON)

$(BIN)/market_data_client: $(BIN) src/market_data/market_data_client.cpp $(COMMON)
	$(CXX) $(CXXFLAGS) -o $@ src/market_data/market_data_client.cpp $(COMMON)

$(BIN)/conn_generator: $(BIN) bonus/conn_generator.cpp $(COMMON)
	$(CXX) $(CXXFLAGS) -o $@ bonus/conn_generator.cpp $(COMMON)

clean:
	rm -rf $(BIN)

.PHONY: all clean
