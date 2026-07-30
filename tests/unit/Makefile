REPO_ROOT := ../..
SECP256K1_SRC := $(REPO_ROOT)/nucula_src/components/secp256k1/libsecp256k1
SECP256K1_INC := $(SECP256K1_SRC)/include
SECP256K1_PRIV_INC := $(SECP256K1_SRC)/src
SECP256K1_CFG := $(REPO_ROOT)/nucula_src/components/secp256k1
CJSON_SRC := $(REPO_ROOT)/../esp/esp-idf/components/json/cJSON
TG_CORE_SRC := $(REPO_ROOT)/components/tollgate_core/src
TG_CORE_INC := $(REPO_ROOT)/components/tollgate_core/include

CC := gcc
CFLAGS := -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-sign-compare \
          -std=gnu17 -g -O0 \
          -DTEST_HOST \
          -DENABLE_MODULE_SCHNORRSIG=1 -DENABLE_MODULE_EXTRAKEYS=1 \
          -DENABLE_MODULE_ECDH=1 \
          -DECMULT_WINDOW_SIZE=8 -DECMULT_GEN_PREC_BITS=4 \
          -include stubs/esp_err.h \
          -I stubs \
          -I $(SECP256K1_INC) \
          -I $(SECP256K1_CFG) \
          -I /usr/include/cjson \
          -I $(TG_CORE_SRC) \
          -I $(TG_CORE_INC)

LDFLAGS := -lmbedcrypto -lcjson -lm

SECP256K1_OBJ := secp256k1.o precomputed_ecmult.o precomputed_ecmult_gen.o

TESTS := test_geohash test_identity test_nostr_event test_cashu test_session test_tollgate_client test_lnurl_pay test_lightning_payout test_mcp_handler test_nip04 test_cvm_server test_beacon_price test_market test_mint_health test_mining_payment test_stratum_proxy test_session_payment_method test_tollgate_client_mining test_firewall_sandbox test_relay_selector test_relay_validator test_portal test_stratum_client test_mint_health_core test_client_core test_relay_types test_sub_manager test_deletion test_mining_api test_stratum_pow test_display test_faucet_client test_remote_miner test_ehash_relay test_stratum_bridge test_relay_simulation

EHASH_RELAY_SRC := $(REPO_ROOT)/components/ehash_relay/src/ehash_relay.c
EHASH_RELAY_INC := $(REPO_ROOT)/components/ehash_relay/include

.PHONY: all test clean $(TESTS)

all: test

test: $(TESTS)
	@echo ""
	@echo "=== Running all unit tests ==="
	@failed=0; \
	for t in $(TESTS); do \
		echo ""; \
		echo "--- $$t ---"; \
		./$$t || failed=$$((failed + 1)); \
	done; \
	echo ""; \
	if [ $$failed -eq 0 ]; then \
		echo "=== ALL UNIT TESTS PASSED ==="; \
	else \
		echo "=== $$failed test(s) FAILED ==="; \
		exit 1; \
	fi

$(SECP256K1_OBJ): %.o: $(SECP256K1_SRC)/src/%.c
	$(CC) $(CFLAGS) -I $(SECP256K1_PRIV_INC) -c $< -o $@

test_geohash: test_geohash.c $(REPO_ROOT)/main/geohash.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test_identity: test_identity.c $(REPO_ROOT)/main/identity.c $(SECP256K1_OBJ)
	$(CC) $(CFLAGS) -I $(SECP256K1_PRIV_INC) $< $(REPO_ROOT)/main/identity.c $(SECP256K1_OBJ) -o $@ $(LDFLAGS)

test_nostr_event: test_nostr_event.c $(REPO_ROOT)/main/nostr_event.c $(REPO_ROOT)/main/identity.c $(SECP256K1_OBJ)
	$(CC) $(CFLAGS) -I $(SECP256K1_PRIV_INC) $< $(REPO_ROOT)/main/nostr_event.c $(REPO_ROOT)/main/identity.c $(SECP256K1_OBJ) -o $@ $(LDFLAGS)

test_cashu: test_cashu.c $(TG_CORE_SRC)/tollgate_core_cashu.c $(TG_CORE_SRC)/tollgate_core_session.c $(TG_CORE_SRC)/tollgate_core_firewall.c $(TG_CORE_SRC)/tollgate_core.c $(TG_CORE_SRC)/tollgate_core_mining.c $(TG_CORE_SRC)/tollgate_core_dns.c $(TG_CORE_SRC)/tollgate_core_stratum_proxy.c
	$(CC) $(CFLAGS) -include stubs/mint_health.h -include stubs/esp_netif.h -include stubs/esp_log.h -include stubs/esp_http_client.h -include stubs/esp_crt_bundle.h $^ -o $@ $(LDFLAGS)

TG_CORE_OBJS_CFLAGS := -include stubs/mint_health.h -include stubs/esp_netif.h -include stubs/esp_log.h -include stubs/esp_http_client.h -include stubs/esp_crt_bundle.h -include stubs/esp_crt_bundle.h

test_session: test_session.c $(TG_CORE_SRC)/tollgate_core_cashu.c $(TG_CORE_SRC)/tollgate_core_session.c $(TG_CORE_SRC)/tollgate_core_firewall.c $(TG_CORE_SRC)/tollgate_core.c $(TG_CORE_SRC)/tollgate_core_mining.c $(TG_CORE_SRC)/tollgate_core_dns.c $(TG_CORE_SRC)/tollgate_core_stratum_proxy.c
	$(CC) $(CFLAGS) $(TG_CORE_OBJS_CFLAGS) $^ -o $@ $(LDFLAGS)

test_tollgate_client: test_tollgate_client.c $(REPO_ROOT)/main/market.c $(REPO_ROOT)/main/beacon_price.c $(TG_CORE_SRC)/tollgate_core_beacon.c $(TG_CORE_SRC)/tollgate_core_market.c $(TG_CORE_SRC)/tollgate_core_client.c
	$(CC) $(CFLAGS) -include stubs/remote_miner.h -I $(REPO_ROOT)/main $< $(REPO_ROOT)/main/market.c $(REPO_ROOT)/main/beacon_price.c $(TG_CORE_SRC)/tollgate_core_beacon.c $(TG_CORE_SRC)/tollgate_core_market.c $(TG_CORE_SRC)/tollgate_core_client.c -o $@ $(LDFLAGS)

test_lnurl_pay: test_lnurl_pay.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

test_lightning_payout: test_lightning_payout.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

test_mcp_handler: test_mcp_handler.c $(REPO_ROOT)/main/mcp_handler.c
	$(CC) $(CFLAGS) -I $(REPO_ROOT)/main -I $(TG_CORE_INC) -I $(TG_CORE_INC)/.. $< $(REPO_ROOT)/main/mcp_handler.c -o $@ $(LDFLAGS)

test_nip04: test_nip04.c $(REPO_ROOT)/main/nip04.c $(SECP256K1_OBJ)
	$(CC) $(CFLAGS) -I $(SECP256K1_PRIV_INC) $< $(REPO_ROOT)/main/nip04.c $(SECP256K1_OBJ) -o $@ $(LDFLAGS)

test_cvm_server: test_cvm_server.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

test_mint_health: test_mint_health.c $(REPO_ROOT)/main/mint_health.c $(TG_CORE_SRC)/tollgate_core_mint_health.c
	$(CC) -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-sign-compare -std=gnu17 -g -O0 -DTEST_HOST -include stubs/esp_err.h -I $(REPO_ROOT)/main -I stubs -I $(SECP256K1_INC) -I $(SECP256K1_CFG) -I /usr/include/cjson -I $(TG_CORE_SRC) -I $(TG_CORE_INC) $^ -o $@ $(LDFLAGS)

test_beacon_price: test_beacon_price.c $(REPO_ROOT)/main/beacon_price.c $(TG_CORE_SRC)/tollgate_core_beacon.c
	$(CC) $(CFLAGS) -I $(REPO_ROOT)/main $< $(REPO_ROOT)/main/beacon_price.c $(TG_CORE_SRC)/tollgate_core_beacon.c -o $@ $(LDFLAGS)

test_market: test_market.c $(REPO_ROOT)/main/market.c $(REPO_ROOT)/main/beacon_price.c $(TG_CORE_SRC)/tollgate_core_beacon.c $(TG_CORE_SRC)/tollgate_core_market.c
	$(CC) $(CFLAGS) -I $(REPO_ROOT)/main $< $(REPO_ROOT)/main/market.c $(REPO_ROOT)/main/beacon_price.c $(TG_CORE_SRC)/tollgate_core_beacon.c $(TG_CORE_SRC)/tollgate_core_market.c -o $@ $(LDFLAGS)

test_mining_payment: test_mining_payment.c $(TG_CORE_SRC)/tollgate_core_mining.c $(TG_CORE_SRC)/tollgate_core.c $(TG_CORE_SRC)/tollgate_core_session.c $(TG_CORE_SRC)/tollgate_core_firewall.c $(TG_CORE_SRC)/tollgate_core_cashu.c $(TG_CORE_SRC)/tollgate_core_dns.c $(TG_CORE_SRC)/tollgate_core_stratum_proxy.c
	$(CC) $(CFLAGS) $(TG_CORE_OBJS_CFLAGS) $^ -o $@ $(LDFLAGS)

test_stratum_proxy: test_stratum_proxy.c $(REPO_ROOT)/main/stratum_proxy.c $(TG_CORE_SRC)/tollgate_core_mining.c $(TG_CORE_SRC)/tollgate_core.c $(TG_CORE_SRC)/tollgate_core_session.c $(TG_CORE_SRC)/tollgate_core_firewall.c $(TG_CORE_SRC)/tollgate_core_cashu.c $(TG_CORE_SRC)/tollgate_core_dns.c $(TG_CORE_SRC)/tollgate_core_stratum_proxy.c
	$(CC) $(CFLAGS) -I $(REPO_ROOT)/main $(TG_CORE_OBJS_CFLAGS) $^ -o $@ $(LDFLAGS)

test_session_payment_method: test_session_payment_method.c $(TG_CORE_SRC)/tollgate_core_cashu.c $(TG_CORE_SRC)/tollgate_core_session.c $(TG_CORE_SRC)/tollgate_core_firewall.c $(TG_CORE_SRC)/tollgate_core.c $(TG_CORE_SRC)/tollgate_core_mining.c $(TG_CORE_SRC)/tollgate_core_dns.c $(TG_CORE_SRC)/tollgate_core_stratum_proxy.c
	$(CC) $(CFLAGS) $(TG_CORE_OBJS_CFLAGS) $^ -o $@ $(LDFLAGS)

test_tollgate_client_mining: test_tollgate_client_mining.c $(TG_CORE_SRC)/tollgate_core_client.c
	$(CC) $(CFLAGS) -include stubs/remote_miner.h -I $(REPO_ROOT)/main $< $(TG_CORE_SRC)/tollgate_core_client.c -o $@ $(LDFLAGS)

test_firewall_sandbox: test_firewall_sandbox.c $(TG_CORE_SRC)/tollgate_core_firewall.c $(TG_CORE_SRC)/tollgate_core.c $(TG_CORE_SRC)/tollgate_core_session.c $(TG_CORE_SRC)/tollgate_core_cashu.c $(TG_CORE_SRC)/tollgate_core_mining.c $(TG_CORE_SRC)/tollgate_core_dns.c $(TG_CORE_SRC)/tollgate_core_stratum_proxy.c
	$(CC) $(CFLAGS) -include stubs/dns_server.h $(TG_CORE_OBJS_CFLAGS) $^ -o $@ $(LDFLAGS)

test_relay_selector: test_relay_selector.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

test_relay_validator: test_relay_validator.c $(REPO_ROOT)/main/nostr_event.c $(REPO_ROOT)/main/identity.c $(REPO_ROOT)/components/wisp_relay/relay_validator.c $(SECP256K1_OBJ)
	$(CC) $(CFLAGS) -I $(SECP256K1_PRIV_INC) -I $(REPO_ROOT)/main -I $(REPO_ROOT)/components/wisp_relay $< $(REPO_ROOT)/main/nostr_event.c $(REPO_ROOT)/main/identity.c $(REPO_ROOT)/components/wisp_relay/relay_validator.c $(SECP256K1_OBJ) -o $@ $(LDFLAGS)

test_portal: test_portal.c $(TG_CORE_SRC)/tollgate_core_portal.c
	$(CC) $(CFLAGS) $(TG_CORE_OBJS_CFLAGS) $^ -o $@ $(LDFLAGS)

test_stratum_client: test_stratum_client.c $(TG_CORE_SRC)/tollgate_core_stratum_client.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test_mint_health_core: test_mint_health_core.c $(TG_CORE_SRC)/tollgate_core_mint_health.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test_client_core: test_client_core.c $(TG_CORE_SRC)/tollgate_core_client.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test_relay_types: test_relay_types.c $(REPO_ROOT)/components/wisp_relay/relay_types.c
	$(CC) $(CFLAGS) -I $(REPO_ROOT)/components/wisp_relay $^ -o $@ $(LDFLAGS)

test_sub_manager: test_sub_manager.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

test_deletion: test_deletion.c $(REPO_ROOT)/components/wisp_relay/relay_types.c
	$(CC) $(CFLAGS) -I $(REPO_ROOT)/components/wisp_relay $^ -o $@ $(LDFLAGS)

test_mining_api: test_mining_api.c $(TG_CORE_SRC)/tollgate_core.c $(TG_CORE_SRC)/tollgate_core_session.c $(TG_CORE_SRC)/tollgate_core_firewall.c $(TG_CORE_SRC)/tollgate_core_cashu.c $(TG_CORE_SRC)/tollgate_core_mining.c $(TG_CORE_SRC)/tollgate_core_dns.c $(TG_CORE_SRC)/tollgate_core_stratum_proxy.c
	$(CC) $(CFLAGS) $(TG_CORE_OBJS_CFLAGS) $^ -o $@ $(LDFLAGS)

test_stratum_pow: test_stratum_pow.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

test_display: test_display.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

test_faucet_client: test_faucet_client.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

test_remote_miner: test_remote_miner.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

test_ehash_relay: test_ehash_relay.c $(EHASH_RELAY_SRC)
	$(CC) $(CFLAGS) -I $(EHASH_RELAY_INC) $^ -o $@ $(LDFLAGS)

test_stratum_bridge: test_stratum_bridge.c $(EHASH_RELAY_SRC) $(TG_CORE_SRC)/tollgate_core_stratum_client.c
	$(CC) $(CFLAGS) -I $(EHASH_RELAY_INC) $^ -o $@ $(LDFLAGS)

test_relay_simulation: test_relay_simulation.c $(EHASH_RELAY_SRC)
	$(CC) $(CFLAGS) -I $(EHASH_RELAY_INC) $^ -o $@ $(LDFLAGS)

clean:
	rm -f $(TESTS) $(SECP256K1_OBJ)
