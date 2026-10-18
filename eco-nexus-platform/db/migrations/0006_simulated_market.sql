-- 0006: SIMULATED market. Nothing here represents a real Carbon Credit Certificate (CCC),
-- a registry record, or settlement. Instrument type 'SIM_CCC' is a demo asset only.
-- Kept in its own tables so it can never be confused with future registry-linked instruments.

CREATE TABLE simulated_accounts (
  id              uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id uuid NOT NULL UNIQUE REFERENCES organizations(id),
  currency        text NOT NULL DEFAULT 'SIM-INR' CHECK (currency = 'SIM-INR'),
  cash_balance    numeric(20,4) NOT NULL DEFAULT 0 CHECK (cash_balance >= 0),
  cash_reserved   numeric(20,4) NOT NULL DEFAULT 0 CHECK (cash_reserved >= 0),
  version         bigint NOT NULL DEFAULT 0,
  created_at      timestamptz NOT NULL DEFAULT now(),
  updated_at      timestamptz NOT NULL DEFAULT now(),
  CONSTRAINT reserved_within_balance CHECK (cash_reserved <= cash_balance)   -- no double spending
);
CREATE TRIGGER simulated_accounts_updated BEFORE UPDATE ON simulated_accounts FOR EACH ROW EXECUTE FUNCTION set_updated_at();

CREATE TABLE simulated_credit_inventory (
  id                uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id   uuid NOT NULL REFERENCES organizations(id),
  instrument_type   text NOT NULL CHECK (instrument_type = 'SIM_CCC'),
  quantity          numeric(20,4) NOT NULL DEFAULT 0 CHECK (quantity >= 0),
  reserved_quantity numeric(20,4) NOT NULL DEFAULT 0 CHECK (reserved_quantity >= 0),
  version           bigint NOT NULL DEFAULT 0,
  updated_at        timestamptz NOT NULL DEFAULT now(),
  UNIQUE (organization_id, instrument_type),
  CONSTRAINT reserved_within_quantity CHECK (reserved_quantity <= quantity)   -- no overselling
);
CREATE TRIGGER simulated_inventory_updated BEFORE UPDATE ON simulated_credit_inventory FOR EACH ROW EXECUTE FUNCTION set_updated_at();

CREATE TABLE simulated_orders (
  sequence           bigint GENERATED ALWAYS AS IDENTITY UNIQUE,    -- deterministic time priority tie-breaker
  id                 uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id    uuid NOT NULL REFERENCES organizations(id),
  instrument_type    text NOT NULL CHECK (instrument_type = 'SIM_CCC'),
  side               text NOT NULL CHECK (side IN ('buy','sell')),
  quantity           numeric(20,4) NOT NULL CHECK (quantity > 0),
  remaining_quantity numeric(20,4) NOT NULL,
  limit_price        numeric(20,4) NOT NULL CHECK (limit_price > 0),
  time_in_force      text NOT NULL DEFAULT 'GTC' CHECK (time_in_force IN ('GTC','IOC','DAY')),
  status             text NOT NULL DEFAULT 'open' CHECK (status IN ('open','partially_filled','filled','cancelled','rejected')),
  version            integer NOT NULL DEFAULT 1 CHECK (version >= 1),
  idempotency_key    text CHECK (idempotency_key IS NULL OR length(idempotency_key) BETWEEN 8 AND 128),
  created_by         uuid NOT NULL REFERENCES users(id),
  created_at         timestamptz NOT NULL DEFAULT now(),
  updated_at         timestamptz NOT NULL DEFAULT now(),
  cancelled_at       timestamptz,
  CHECK (remaining_quantity >= 0 AND remaining_quantity <= quantity),
  CONSTRAINT filled_iff_nothing_remaining CHECK ((status = 'filled') = (remaining_quantity = 0)),
  CONSTRAINT partial_has_fills CHECK (status <> 'partially_filled' OR remaining_quantity < quantity),
  CONSTRAINT open_untouched CHECK (status <> 'open' OR remaining_quantity = quantity),
  CONSTRAINT cancelled_has_timestamp CHECK ((status = 'cancelled') = (cancelled_at IS NOT NULL)),
  UNIQUE (id, organization_id)
);
CREATE TRIGGER simulated_orders_updated BEFORE UPDATE ON simulated_orders FOR EACH ROW EXECUTE FUNCTION set_updated_at();
CREATE UNIQUE INDEX simulated_orders_idem_key ON simulated_orders (organization_id, idempotency_key) WHERE idempotency_key IS NOT NULL;
-- Matching path: best price first, then earliest sequence (price-time priority).
CREATE INDEX simulated_orders_book_idx ON simulated_orders (instrument_type, side, limit_price, sequence)
  WHERE status IN ('open','partially_filled');
CREATE INDEX simulated_orders_org_idx ON simulated_orders (organization_id, created_at DESC);

CREATE TABLE simulated_trades (
  sequence        bigint GENERATED ALWAYS AS IDENTITY UNIQUE,
  id              uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  instrument_type text NOT NULL CHECK (instrument_type = 'SIM_CCC'),
  buy_order_id    uuid NOT NULL,
  sell_order_id   uuid NOT NULL,
  buyer_org_id    uuid NOT NULL,
  seller_org_id   uuid NOT NULL,
  quantity        numeric(20,4) NOT NULL CHECK (quantity > 0),
  price           numeric(20,4) NOT NULL CHECK (price > 0),
  notional        numeric(28,8) NOT NULL,
  executed_at     timestamptz NOT NULL DEFAULT now(),
  CONSTRAINT notional_exact CHECK (notional = quantity * price),
  CONSTRAINT no_self_trade CHECK (buyer_org_id <> seller_org_id),
  FOREIGN KEY (buy_order_id,  buyer_org_id)  REFERENCES simulated_orders (id, organization_id),
  FOREIGN KEY (sell_order_id, seller_org_id) REFERENCES simulated_orders (id, organization_id)
);
CREATE INDEX simulated_trades_buy_idx  ON simulated_trades (buy_order_id);
CREATE INDEX simulated_trades_sell_idx ON simulated_trades (sell_order_id);
CREATE INDEX simulated_trades_time_idx ON simulated_trades (executed_at DESC);

-- A trade must pair a buy order with a sell order of the same instrument, and is immutable afterwards.
CREATE FUNCTION simulated_trades_guard() RETURNS trigger LANGUAGE plpgsql AS $$
DECLARE b simulated_orders; s simulated_orders;
BEGIN
  IF TG_OP <> 'INSERT' THEN
    RAISE EXCEPTION 'simulated trades are immutable' USING ERRCODE = 'integrity_constraint_violation';
  END IF;
  SELECT * INTO b FROM simulated_orders WHERE id = NEW.buy_order_id;
  SELECT * INTO s FROM simulated_orders WHERE id = NEW.sell_order_id;
  IF b.side <> 'buy' OR s.side <> 'sell' OR b.instrument_type <> NEW.instrument_type OR s.instrument_type <> NEW.instrument_type THEN
    RAISE EXCEPTION 'trade must pair a buy and a sell order of instrument %', NEW.instrument_type USING ERRCODE = 'integrity_constraint_violation';
  END IF;
  IF NEW.price > b.limit_price OR NEW.price < s.limit_price THEN
    RAISE EXCEPTION 'trade price % outside the limits of the crossing orders', NEW.price USING ERRCODE = 'integrity_constraint_violation';
  END IF;
  RETURN NEW;
END $$;
CREATE TRIGGER simulated_trades_integrity BEFORE INSERT OR UPDATE OR DELETE ON simulated_trades
  FOR EACH ROW EXECUTE FUNCTION simulated_trades_guard();
