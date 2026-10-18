// Database-level guarantees: constraints, tenant isolation and immutability triggers.
// All fixture data below is FABRICATED for tests. Every case runs in its own rolled-back transaction.
#include "carbon/db/migrator.hpp"

#include "test_support.hpp"

#include <memory>

using namespace carbon;

namespace {
const char* kArgon = "$argon2id$v=19$m=65536,t=3,p=4$ZmFrZXNhbHRmb3J0ZXN0$ZmFrZWhhc2hmb3J0ZXN0c29ubHk";

class Schema : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    if (!tsup::adminUrl()) return;
    db_ = std::make_unique<tsup::TempDatabase>();
    pqxx::connection c(db_->url());
    db::applyMigrations(c, db::loadMigrations(tsup::migrationsDir()));
  }
  static void TearDownTestSuite() { db_.reset(); }

  // Runs body in a fresh transaction (never committed) and expects it to fail with `state`.
  template <class F>
  void expectState(const std::string& state, F&& body) {
    REQUIRE_TEST_DB();
    pqxx::connection c(db_->url());
    pqxx::work tx(c);
    try {
      body(tx);
    } catch (const pqxx::sql_error& e) {
      EXPECT_EQ(e.sqlstate(), state) << e.what();
      return;
    }
    ADD_FAILURE() << "expected SQLSTATE " << state << " but statements succeeded";
  }
  template <class F>
  void expectOk(F&& body) {
    REQUIRE_TEST_DB();
    pqxx::connection c(db_->url());
    pqxx::work tx(c);
    EXPECT_NO_THROW(body(tx));
  }

  static std::string one(pqxx::work& tx, const std::string& sql) { return tx.exec1(sql)[0].as<std::string>(); }

  struct Seed { std::string userA, userB, orgA, orgB, facA, facB; };
  static Seed seed(pqxx::work& tx) {
    Seed s;
    s.userA = one(tx, std::string("INSERT INTO users(email,password_hash,display_name) VALUES ('a@fixture.test','") + kArgon + "','User A') RETURNING id");
    s.userB = one(tx, std::string("INSERT INTO users(email,password_hash,display_name) VALUES ('b@fixture.test','") + kArgon + "','User B') RETURNING id");
    s.orgA = one(tx, "INSERT INTO organizations(name,sector) VALUES ('Org A (fixture)','cement') RETURNING id");
    s.orgB = one(tx, "INSERT INTO organizations(name,sector) VALUES ('Org B (fixture)','steel') RETURNING id");
    s.facA = one(tx, "INSERT INTO facilities(organization_id,name,sector,production_unit) VALUES ('" + s.orgA + "','Plant A','cement','tonne') RETURNING id");
    s.facB = one(tx, "INSERT INTO facilities(organization_id,name,sector,production_unit) VALUES ('" + s.orgB + "','Plant B','steel','tonne') RETURNING id");
    return s;
  }
  static std::string activity(pqxx::work& tx, const Seed& s, const std::string& org, const std::string& fac, const std::string& hashChar, const std::string& qty = "100") {
    return one(tx, "INSERT INTO activity_records(organization_id,facility_id,period_start,period_end,source_type,activity_key,quantity,unit,data_quality,dedupe_hash,created_by) VALUES ('" +
                       org + "','" + fac + "','2025-04-01','2026-03-31','stationary_combustion','fuel.coal'," + qty + ",'tonne','measured','" + std::string(64, hashChar[0]) + "','" + s.userA + "') RETURNING id");
  }
  static std::string calcRun(pqxx::work& tx, const Seed& s, const std::string& org, const std::string& fac) {
    return one(tx, "INSERT INTO calculation_runs(organization_id,facility_id,period_start,period_end,method_version,scope1_tco2e,scope2_location_tco2e,total_tco2e,production_quantity,production_unit,intensity,intensity_unit,created_by) VALUES ('" +
                       org + "','" + fac + "','2025-04-01','2026-03-31','test-1',10,5,15,100,'tonne',0.15,'tCO2e/tonne','" + s.userA + "') RETURNING id");
  }
  static std::string factor(pqxx::work& tx) {
    const std::string src = one(tx, "INSERT INTO emission_factor_sources(name,publisher,origin) VALUES ('FIXTURE source','test','development_fixture') RETURNING id");
    return one(tx, "INSERT INTO emission_factors(source_id,factor_key,version,name,scope,category,activity_unit,basis,factor_co2e_kg,valid_from) VALUES ('" + src +
                       "','fuel.fixture',1,'Fixture fuel (not a real factor)',1,'stationary_fuel','kg','co2e',2.5,'2025-01-01') RETURNING id");
  }
  static std::string order(pqxx::work& tx, const Seed& s, const std::string& org, const std::string& side, const std::string& price, const std::string& key = "") {
    return one(tx, "INSERT INTO simulated_orders(organization_id,instrument_type,side,quantity,remaining_quantity,limit_price,created_by,idempotency_key) VALUES ('" + org +
                       "','SIM_CCC','" + side + "',10,10," + price + ",'" + s.userA + "'," + (key.empty() ? "NULL" : "'" + key + "'") + ") RETURNING id");
  }

  static std::unique_ptr<tsup::TempDatabase> db_;
};
std::unique_ptr<tsup::TempDatabase> Schema::db_;
}  // namespace

TEST_F(Schema, AllRequiredTablesExist) {
  REQUIRE_TEST_DB();
  const char* required[] = {"users", "organizations", "organization_memberships", "facilities", "activity_records", "production_records",
                            "emission_factors", "emission_factor_sources", "calculation_runs", "calculation_inputs", "regulatory_targets",
                            "regulatory_rule_versions", "compliance_assessments", "evidence_files", "consultancy_reviews", "investment_projects",
                            "investment_scenarios", "simulated_credit_inventory", "simulated_orders", "simulated_trades", "simulated_accounts",
                            "audit_events", "external_data_sources", "imported_datasets"};
  pqxx::connection c(db_->url());
  pqxx::nontransaction tx(c);
  for (const char* t : required) EXPECT_TRUE(tx.exec_params1("SELECT to_regclass($1) IS NOT NULL", std::string("public.") + t)[0].as<bool>()) << t;
}

TEST_F(Schema, SeedFixturesAreAccepted) { expectOk([](pqxx::work& tx) { seed(tx); }); }

TEST_F(Schema, PlaintextPasswordIsRejected) {
  expectState("23514", [](pqxx::work& tx) { tx.exec0("INSERT INTO users(email,password_hash,display_name) VALUES ('p@fixture.test','plaintext-password','P')"); });
}

TEST_F(Schema, EmailUniqueIgnoringCase) {
  expectState("23505", [](pqxx::work& tx) {
    seed(tx);
    tx.exec0(std::string("INSERT INTO users(email,password_hash,display_name) VALUES ('A@FIXTURE.TEST','") + kArgon + "','Dup')");
  });
}

TEST_F(Schema, ChildCannotReferenceAnotherTenantsFacility) {
  expectState("23503", [](pqxx::work& tx) {
    const Seed s = seed(tx);
    activity(tx, s, s.orgA, s.facB, "a");  // org A row pointing at org B's facility
  });
}

TEST_F(Schema, NegativeActivityQuantityRejected) {
  expectState("23514", [](pqxx::work& tx) { const Seed s = seed(tx); activity(tx, s, s.orgA, s.facA, "a", "-1"); });
}

TEST_F(Schema, DuplicateActivityDetectedUnlessVoided) {
  expectState("23505", [](pqxx::work& tx) {
    const Seed s = seed(tx);
    activity(tx, s, s.orgA, s.facA, "b");
    activity(tx, s, s.orgA, s.facA, "b");
  });
  expectOk([](pqxx::work& tx) {
    const Seed s = seed(tx);
    const std::string id = activity(tx, s, s.orgA, s.facA, "c");
    tx.exec0("UPDATE activity_records SET voided_at = now(), voided_by = '" + s.userA + "' WHERE id = '" + id + "'");
    activity(tx, s, s.orgA, s.facA, "c");  // allowed once the original is voided
  });
}

TEST_F(Schema, CalculationInputCannotCrossTenants) {
  expectState("23503", [](pqxx::work& tx) {
    const Seed s = seed(tx);
    const std::string run = calcRun(tx, s, s.orgA, s.facA);
    const std::string ef = factor(tx);
    tx.exec0("INSERT INTO calculation_inputs(organization_id,calculation_run_id,source_type,activity_key,original_quantity,original_unit,conversion_factor,converted_quantity,converted_unit,emission_factor_id,emission_factor_version,factor_snapshot,source_reference,scope,emissions_tco2e) VALUES ('" +
             s.orgB + "','" + run + "','stationary_combustion','fuel.fixture',1,'kg',1,1,'kg','" + ef + "',1,'{}','fixture',1,0)");
  });
}

TEST_F(Schema, FinalizedCalculationRunIsImmutableButWorkflowMayProgress) {
  expectOk([](pqxx::work& tx) {
    const Seed s = seed(tx);
    const std::string run = calcRun(tx, s, s.orgA, s.facA);
    tx.exec0("UPDATE calculation_runs SET scope1_tco2e = 11 WHERE id = '" + run + "'");  // still a draft: editable
    tx.exec0("UPDATE calculation_runs SET finalized_at = now() WHERE id = '" + run + "'");
    tx.exec0("UPDATE calculation_runs SET status = 'submitted' WHERE id = '" + run + "'");  // workflow field: allowed
  });
  expectState("23000", [](pqxx::work& tx) {
    const Seed s = seed(tx);
    const std::string run = calcRun(tx, s, s.orgA, s.facA);
    tx.exec0("UPDATE calculation_runs SET finalized_at = now() WHERE id = '" + run + "'");
    tx.exec0("UPDATE calculation_runs SET scope1_tco2e = 99 WHERE id = '" + run + "'");
  });
  expectState("23000", [](pqxx::work& tx) {
    const Seed s = seed(tx);
    const std::string run = calcRun(tx, s, s.orgA, s.facA);
    tx.exec0("UPDATE calculation_runs SET finalized_at = now() WHERE id = '" + run + "'");
    tx.exec0("DELETE FROM calculation_runs WHERE id = '" + run + "'");
  });
  expectState("23000", [](pqxx::work& tx) {  // inputs of a finalized run are frozen too
    const Seed s = seed(tx);
    const std::string run = calcRun(tx, s, s.orgA, s.facA);
    const std::string ef = factor(tx);
    tx.exec0("UPDATE calculation_runs SET finalized_at = now() WHERE id = '" + run + "'");
    tx.exec0("INSERT INTO calculation_inputs(organization_id,calculation_run_id,source_type,activity_key,original_quantity,original_unit,conversion_factor,converted_quantity,converted_unit,emission_factor_id,emission_factor_version,factor_snapshot,source_reference,scope,emissions_tco2e) VALUES ('" +
             s.orgA + "','" + run + "','stationary_combustion','fuel.fixture',1,'kg',1,1,'kg','" + ef + "',1,'{}','fixture',1,0)");
  });
}

TEST_F(Schema, ApprovedRunMustBeFinalized) {
  expectState("23514", [](pqxx::work& tx) {
    const Seed s = seed(tx);
    const std::string run = calcRun(tx, s, s.orgA, s.facA);
    tx.exec0("UPDATE calculation_runs SET status='approved', approved_by='" + s.userB + "', approved_at=now() WHERE id='" + run + "'");
  });
}

TEST_F(Schema, ActiveEmissionFactorIsFrozen) {
  expectOk([](pqxx::work& tx) {
    const std::string ef = factor(tx);
    tx.exec0("UPDATE emission_factors SET factor_co2e_kg = 3.1 WHERE id = '" + ef + "'");  // draft: editable
    tx.exec0("UPDATE emission_factors SET status='active', activated_at=now() WHERE id = '" + ef + "'");
    tx.exec0("UPDATE emission_factors SET status='retired', valid_to='2026-01-01' WHERE id = '" + ef + "'");
  });
  expectState("23000", [](pqxx::work& tx) {
    const std::string ef = factor(tx);
    tx.exec0("UPDATE emission_factors SET status='active', activated_at=now() WHERE id = '" + ef + "'");
    tx.exec0("UPDATE emission_factors SET factor_co2e_kg = 9 WHERE id = '" + ef + "'");
  });
  expectState("23000", [](pqxx::work& tx) {
    const std::string ef = factor(tx);
    tx.exec0("UPDATE emission_factors SET status='active', activated_at=now() WHERE id = '" + ef + "'");
    tx.exec0("DELETE FROM emission_factors WHERE id = '" + ef + "'");
  });
  expectState("23000", [](pqxx::work& tx) {  // a retired factor cannot come back
    const std::string ef = factor(tx);
    tx.exec0("UPDATE emission_factors SET status='active', activated_at=now() WHERE id = '" + ef + "'");
    tx.exec0("UPDATE emission_factors SET status='retired' WHERE id = '" + ef + "'");
    tx.exec0("UPDATE emission_factors SET status='active' WHERE id = '" + ef + "'");
  });
}

TEST_F(Schema, EmissionFactorShapeRules) {
  expectState("23514", [](pqxx::work& tx) {  // co2e basis must not also carry per-gas values
    const std::string src = one(tx, "INSERT INTO emission_factor_sources(name,publisher,origin) VALUES ('F','t','development_fixture') RETURNING id");
    tx.exec0("INSERT INTO emission_factors(source_id,factor_key,version,name,scope,category,activity_unit,basis,factor_co2e_kg,co2_kg,valid_from) VALUES ('" + src + "','k',1,'n',1,'other','kg','co2e',1,1,'2025-01-01')");
  });
  expectState("23514", [](pqxx::work& tx) {  // scope 2 needs a location/market method
    const std::string src = one(tx, "INSERT INTO emission_factor_sources(name,publisher,origin) VALUES ('F','t','development_fixture') RETURNING id");
    tx.exec0("INSERT INTO emission_factors(source_id,factor_key,version,name,scope,category,activity_unit,basis,factor_co2e_kg,valid_from) VALUES ('" + src + "','k',1,'n',2,'electricity_grid','kWh','co2e',1,'2025-01-01')");
  });
  expectState("23514", [](pqxx::work& tx) {  // per-gas factors must name the GWP set that applies
    const std::string src = one(tx, "INSERT INTO emission_factor_sources(name,publisher,origin) VALUES ('F','t','development_fixture') RETURNING id");
    tx.exec0("INSERT INTO emission_factors(source_id,factor_key,version,name,scope,category,activity_unit,basis,co2_kg,ch4_kg,valid_from) VALUES ('" + src + "','k',1,'n',1,'other','kg','by_gas',1,0.1,'2025-01-01')");
  });
}

TEST_F(Schema, ComplianceAssessmentCanOnlyBeAnEstimate) {
  auto base = [](pqxx::work& tx, const std::string& legal, const std::string& shortfall, const std::string& surplus) {
    const Seed s = seed(tx);
    const std::string run = calcRun(tx, s, s.orgA, s.facA);
    const std::string rv = one(tx, "INSERT INTO regulatory_rule_versions(version_label,title,origin) VALUES ('fixture-v1','FIXTURE rule (not real)','development_fixture') RETURNING id");
    const std::string tg = one(tx, "INSERT INTO regulatory_targets(rule_version_id,sector,target_year,intensity_target,production_unit) VALUES ('" + rv + "','cement',2026,0.5,'tonne') RETURNING id");
    tx.exec0("INSERT INTO compliance_assessments(organization_id,facility_id,calculation_run_id,regulatory_target_id,eligible_production,regulated_emissions_tco2e,benchmark_emissions_tco2e,shortfall_tco2e,surplus_tco2e,legal_status,created_by) VALUES ('" +
             s.orgA + "','" + s.facA + "','" + run + "','" + tg + "',100,60,50," + shortfall + "," + surplus + ",'" + legal + "','" + s.userA + "')");
  };
  expectOk([&](pqxx::work& tx) { base(tx, "estimate_only", "10", "0"); });
  expectState("23514", [&](pqxx::work& tx) { base(tx, "verified", "10", "0"); });          // cannot claim legal verification
  expectState("23514", [&](pqxx::work& tx) { base(tx, "estimate_only", "10", "5"); });     // shortfall and surplus are exclusive
}

TEST_F(Schema, RegulatoryVerificationNeedsAVerifier) {
  expectState("23514", [](pqxx::work& tx) {
    tx.exec0("INSERT INTO regulatory_rule_versions(version_label,title,origin,verification_status) VALUES ('v','t','development_fixture','verified')");
  });
}

TEST_F(Schema, ManualImportsAreNeverLiveAndApisNeedVerification) {
  expectState("23514", [](pqxx::work& tx) {
    const std::string src = one(tx, "INSERT INTO external_data_sources(source_key,name,publisher,access_kind) VALUES ('s1','S','P','manual_import_only') RETURNING id");
    tx.exec0("INSERT INTO imported_datasets(source_id,label,sha256,import_method,is_live,retrieved_at) VALUES ('" + src + "','x','" + std::string(64, 'a') + "','manual_file',true,now())");
  });
  expectState("23514", [](pqxx::work& tx) {
    tx.exec0("INSERT INTO external_data_sources(source_key,name,publisher,access_kind,endpoint_url) VALUES ('s2','S','P','documented_api','https://example.test/api')");
  });
}

TEST_F(Schema, SimulatedBalancesCannotBeOverReserved) {
  expectState("23514", [](pqxx::work& tx) {
    const Seed s = seed(tx);
    tx.exec0("INSERT INTO simulated_accounts(organization_id,cash_balance,cash_reserved) VALUES ('" + s.orgA + "',100,100.0001)");
  });
  expectState("23514", [](pqxx::work& tx) {
    const Seed s = seed(tx);
    tx.exec0("INSERT INTO simulated_credit_inventory(organization_id,instrument_type,quantity,reserved_quantity) VALUES ('" + s.orgA + "','SIM_CCC',5,6)");
  });
  expectState("23514", [](pqxx::work& tx) {  // real-looking instruments cannot sneak into the simulation tables
    const Seed s = seed(tx);
    tx.exec0("INSERT INTO simulated_credit_inventory(organization_id,instrument_type,quantity) VALUES ('" + s.orgA + "','CCC',5)");
  });
}

TEST_F(Schema, OrderStateInvariants) {
  expectState("23514", [](pqxx::work& tx) {
    const Seed s = seed(tx);
    tx.exec0("INSERT INTO simulated_orders(organization_id,instrument_type,side,quantity,remaining_quantity,limit_price,status,created_by) VALUES ('" + s.orgA + "','SIM_CCC','buy',10,4,100,'filled','" + s.userA + "')");
  });
  expectState("23514", [](pqxx::work& tx) {
    const Seed s = seed(tx);
    tx.exec0("INSERT INTO simulated_orders(organization_id,instrument_type,side,quantity,remaining_quantity,limit_price,status,created_by) VALUES ('" + s.orgA + "','SIM_CCC','buy',10,4,100,'open','" + s.userA + "')");
  });
  expectState("23514", [](pqxx::work& tx) {
    const Seed s = seed(tx);
    tx.exec0("INSERT INTO simulated_orders(organization_id,instrument_type,side,quantity,remaining_quantity,limit_price,created_by) VALUES ('" + s.orgA + "','SIM_CCC','sell',10,10,0,'" + s.userA + "')");
  });
}

TEST_F(Schema, IdempotencyKeyUniquePerOrganisation) {
  expectState("23505", [](pqxx::work& tx) {
    const Seed s = seed(tx);
    order(tx, s, s.orgA, "buy", "100", "idem-key-0001");
    order(tx, s, s.orgA, "buy", "100", "idem-key-0001");
  });
  expectOk([](pqxx::work& tx) {
    const Seed s = seed(tx);
    order(tx, s, s.orgA, "buy", "100", "idem-key-0001");
    order(tx, s, s.orgB, "buy", "100", "idem-key-0001");
  });
}

TEST_F(Schema, TradeIntegrity) {
  auto trade = [](pqxx::work& tx, const Seed& s, const std::string& buyOrder, const std::string& sellOrder, const std::string& buyerOrg,
                  const std::string& sellerOrg, const std::string& price) {
    tx.exec0("INSERT INTO simulated_trades(instrument_type,buy_order_id,sell_order_id,buyer_org_id,seller_org_id,quantity,price,notional) VALUES ('SIM_CCC','" +
             buyOrder + "','" + sellOrder + "','" + buyerOrg + "','" + sellerOrg + "',10," + price + ",10*" + price + ")");
    (void)s;
  };
  expectOk([&](pqxx::work& tx) {
    const Seed s = seed(tx);
    trade(tx, s, order(tx, s, s.orgA, "buy", "105"), order(tx, s, s.orgB, "sell", "100"), s.orgA, s.orgB, "102");
  });
  expectState("23514", [&](pqxx::work& tx) {  // self trade
    const Seed s = seed(tx);
    trade(tx, s, order(tx, s, s.orgA, "buy", "105"), order(tx, s, s.orgA, "sell", "100"), s.orgA, s.orgA, "102");
  });
  expectState("23000", [&](pqxx::work& tx) {  // price outside the crossing orders' limits
    const Seed s = seed(tx);
    trade(tx, s, order(tx, s, s.orgA, "buy", "105"), order(tx, s, s.orgB, "sell", "100"), s.orgA, s.orgB, "110");
  });
  expectState("23000", [&](pqxx::work& tx) {  // two buy orders cannot be paired
    const Seed s = seed(tx);
    trade(tx, s, order(tx, s, s.orgA, "buy", "105"), order(tx, s, s.orgB, "buy", "100"), s.orgA, s.orgB, "102");
  });
  expectState("23503", [&](pqxx::work& tx) {  // buyer org must own the buy order
    const Seed s = seed(tx);
    trade(tx, s, order(tx, s, s.orgA, "buy", "105"), order(tx, s, s.orgB, "sell", "100"), s.orgB, s.orgA, "102");
  });
  expectState("23000", [&](pqxx::work& tx) {  // trades are immutable
    const Seed s = seed(tx);
    trade(tx, s, order(tx, s, s.orgA, "buy", "105"), order(tx, s, s.orgB, "sell", "100"), s.orgA, s.orgB, "102");
    tx.exec0("UPDATE simulated_trades SET price = 101");
  });
}

TEST_F(Schema, AuditEventsAreAppendOnly) {
  expectOk([](pqxx::work& tx) { tx.exec0("INSERT INTO audit_events(action,details) VALUES ('test.event','{\"k\":1}')"); });
  expectState("23000", [](pqxx::work& tx) {
    tx.exec0("INSERT INTO audit_events(action) VALUES ('test.event')");
    tx.exec0("UPDATE audit_events SET action = 'tampered'");
  });
  expectState("23000", [](pqxx::work& tx) {
    tx.exec0("INSERT INTO audit_events(action) VALUES ('test.event')");
    tx.exec0("DELETE FROM audit_events");
  });
  expectState("23000", [](pqxx::work& tx) { tx.exec0("TRUNCATE audit_events"); });
}

TEST_F(Schema, EvidenceFileRules) {
  auto ins = [](pqxx::work& tx, const std::string& fname, const std::string& size, bool crossTenantRun) {
    const Seed s = seed(tx);
    const std::string run = calcRun(tx, s, s.orgA, s.facA);
    tx.exec0("INSERT INTO evidence_files(organization_id,calculation_run_id,original_filename,storage_key,content_type,size_bytes,sha256,uploaded_by) VALUES ('" +
             (crossTenantRun ? s.orgB : s.orgA) + "','" + run + "','" + fname + "','key1','application/pdf'," + size + ",'" + std::string(64, 'f') + "','" + s.userA + "')");
  };
  expectOk([&](pqxx::work& tx) { ins(tx, "invoice.pdf", "1000", false); });
  expectState("23514", [&](pqxx::work& tx) { ins(tx, "../etc/passwd", "1000", false); });   // path traversal
  expectState("23514", [&](pqxx::work& tx) { ins(tx, "big.pdf", "26214401", false); });     // > 25 MiB
  expectState("23503", [&](pqxx::work& tx) { ins(tx, "x.pdf", "10", true); });              // other tenant's run
}

TEST_F(Schema, OnlyOneActiveReviewPerReviewerAndRun) {
  auto review = [](pqxx::work& tx, const Seed& s, const std::string& run) {
    tx.exec0("INSERT INTO consultancy_reviews(organization_id,calculation_run_id,reviewer_id,assigned_by) VALUES ('" + s.orgA + "','" + run + "','" + s.userB + "','" + s.userA + "')");
  };
  expectState("23505", [&](pqxx::work& tx) {
    const Seed s = seed(tx);
    const std::string run = calcRun(tx, s, s.orgA, s.facA);
    review(tx, s, run);
    review(tx, s, run);
  });
  expectState("23514", [](pqxx::work& tx) {  // a decided review needs decided_at
    const Seed s = seed(tx);
    const std::string run = calcRun(tx, s, s.orgA, s.facA);
    tx.exec0("INSERT INTO consultancy_reviews(organization_id,calculation_run_id,reviewer_id,assigned_by,status) VALUES ('" + s.orgA + "','" + run + "','" + s.userB + "','" + s.userA + "','approved')");
  });
}

TEST_F(Schema, SessionTokensMustBeHashedAndCsrfTokenStrong) {
  auto ins = [](pqxx::work& tx, const std::string& hash, const std::string& csrf) {
    const Seed s = seed(tx);
    tx.exec0("INSERT INTO sessions(user_id,token_hash,csrf_token,expires_at) VALUES ('" + s.userA + "','" + hash + "','" + csrf + "', now() + interval '1 hour')");
  };
  expectOk([&](pqxx::work& tx) { ins(tx, std::string(64, 'a'), std::string(64, 'b')); });
  expectState("23514", [&](pqxx::work& tx) { ins(tx, "raw-session-token-not-a-hash", std::string(64, 'b')); });  // a raw token cannot be stored
  expectState("23514", [&](pqxx::work& tx) { ins(tx, std::string(64, 'a'), "weak"); });
  expectState("23514", [&](pqxx::work& tx) {  // expiry must be after creation
    const Seed s = seed(tx);
    tx.exec0("INSERT INTO sessions(user_id,token_hash,csrf_token,expires_at) VALUES ('" + s.userA + "','" + std::string(64, 'a') + "','" + std::string(64, 'b') + "', now() - interval '1 hour')");
  });
  expectState("23505", [&](pqxx::work& tx) { ins(tx, std::string(64, 'c'), std::string(64, 'd')); ins(tx, std::string(64, 'c'), std::string(64, 'd')); });
}

TEST_F(Schema, ResetTokensMustBeHashed) {
  expectState("23514", [](pqxx::work& tx) {
    const Seed s = seed(tx);
    tx.exec0("INSERT INTO password_reset_tokens(user_id,token_hash,expires_at) VALUES ('" + s.userA + "','plain-reset-token', now() + interval '1 hour')");
  });
  expectOk([](pqxx::work& tx) {
    const Seed s = seed(tx);
    tx.exec0("INSERT INTO password_reset_tokens(user_id,token_hash,expires_at) VALUES ('" + s.userA + "','" + std::string(64, 'e') + "', now() + interval '1 hour')");
  });
}
