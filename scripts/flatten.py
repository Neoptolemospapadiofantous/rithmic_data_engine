"""
flatten.py — Cancel open stop order and market-sell to go flat.

Usage:
    python scripts/flatten.py [--basket-id BASKET_ID]
    python scripts/flatten.py --help
"""
import argparse
import asyncio
import logging
import sys
import uuid
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent.parent / "bot"))

from python.rithmic.client import RithmicConfig, get_client

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger("flatten")

ACCOUNT_ID = "LTARAPAPA502114908626"
SYMBOL     = "NQM6"
EXCHANGE   = "CME"
_DEFAULT_STOP_BASKET = "1275735"


def parse_args(argv=None):
    parser = argparse.ArgumentParser(
        prog="flatten.py",
        description=(
            "Cancel an open stop order and submit a market SELL to go flat. "
            "Connects to Rithmic via ORDER_PLANT using credentials from the environment."
        ),
    )
    parser.add_argument(
        "basket_id",
        nargs="?",
        default=_DEFAULT_STOP_BASKET,
        metavar="BASKET_ID",
        help=(
            "Basket ID of the stop order to cancel before flattening. "
            f"Defaults to {_DEFAULT_STOP_BASKET!r} when omitted."
        ),
    )
    return parser.parse_args(argv)


async def run(stop_basket: str):
    from async_rithmic import SysInfraType, TransactionType, OrderType

    cfg = RithmicConfig.from_env()
    log.info(f"Connecting: user={cfg.user} system={cfg.system_name}")

    client = get_client(cfg)

    async def on_notify(n):
        log.info(f"NOTIFY: {n}")
    client.on_rithmic_order_notification += on_notify
    client.on_exchange_order_notification += on_notify

    await client.connect(plants=[SysInfraType.ORDER_PLANT])
    log.info("ORDER_PLANT connected")

    order_plant = client.plants["order"]

    # Step 1: cancel the stop order
    if stop_basket:
        log.info(f"Cancelling stop basket={stop_basket}")
        try:
            await order_plant.cancel_order(
                basket_id=stop_basket,
                account_id=ACCOUNT_ID,
            )
            log.info("Cancel submitted")
        except Exception as e:
            log.warning(f"Cancel failed (may already be gone): {e}")
        await asyncio.sleep(2)

    # Step 2: market SELL 1 to flatten
    sell_id = str(uuid.uuid4())
    log.info(f"MARKET SELL 1 {SYMBOL} id={sell_id[:8]}")
    try:
        await order_plant.submit_order(
            order_id=sell_id,
            symbol=SYMBOL,
            exchange=EXCHANGE,
            qty=1,
            transaction_type=TransactionType.SELL,
            order_type=OrderType.MARKET,
            account_id=ACCOUNT_ID,
        )
        log.info("SELL submitted")
    except Exception as e:
        log.error(f"SELL failed: {e}")

    await asyncio.sleep(5)
    log.info("Done — position should be flat")
    await client.disconnect()


if __name__ == "__main__":
    args = parse_args()
    asyncio.run(run(stop_basket=args.basket_id))
