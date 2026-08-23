# ============================================================
# stock-tv webhook daemon
# 聚合 腾讯行情(股票/指数/分时) + 天天基金(场外基金估值)
# 输出设备端约定的极简 JSON,局域网纯 HTTP,无鉴权
#
# 运行:  pip install -r requirements.txt
#        python app.py            (默认 0.0.0.0:8899)
#
# 接口:  GET /quote?symbol=sh600519&points=48
#        GET /quote?symbol=f005827
#        GET /health
# ============================================================
import re
import time
import json
import requests
from flask import Flask, request, jsonify

app = Flask(__name__)

UA = {"User-Agent": "Mozilla/5.0 (stock-tv daemon)"}
QUOTE_TTL = 30      # 行情缓存秒数(避免高频轮询打接口)
SPARK_TTL = 120     # 分时缓存秒数
_cache = {}         # key -> (expire_ts, data)


def cache_get(key):
    hit = _cache.get(key)
    if hit and hit[0] > time.time():
        return hit[1]
    return None


def cache_set(key, data, ttl):
    _cache[key] = (time.time() + ttl, data)


# ---------------- 腾讯行情: 股票/指数 ----------------
# http://qt.gtimg.cn/q=sh600519  返回 GBK 编码的 ~ 分隔文本
# 字段: [1]名称 [3]现价 [4]昨收 [31]涨跌额 [32]涨跌幅%
def fetch_stock(symbol):
    key = f"q:{symbol}"
    hit = cache_get(key)
    if hit:
        return hit
    r = requests.get(f"http://qt.gtimg.cn/q={symbol}", headers=UA, timeout=5)
    r.encoding = "gbk"
    m = re.search(r'"(.*)"', r.text)
    if not m:
        return None
    f = m.group(1).split("~")
    if len(f) < 33 or not f[3]:
        return None
    data = {
        "symbol": symbol,
        "name": f[1],
        "price": float(f[3]),
        "change": float(f[31]),
        "changePct": float(f[32]),
        "ok": True,
    }
    cache_set(key, data, QUOTE_TTL)
    return data


# 分时数据(当日走势) → 采样为 N 点 sparkline
def fetch_spark(symbol, points):
    key = f"s:{symbol}:{points}"
    hit = cache_get(key)
    if hit is not None:
        return hit
    try:
        r = requests.get(
            "http://web.ifzq.gtimg.cn/appstock/app/minute/query",
            params={"code": symbol}, headers=UA, timeout=5)
        j = r.json()
        raw = j["data"][symbol]["data"]["data"]  # ["0930 12.34 vol", ...]
        prices = [float(line.split(" ")[1]) for line in raw if " " in line]
    except Exception:
        prices = []
    if len(prices) > points:  # 均匀降采样
        step = len(prices) / points
        prices = [prices[int(i * step)] for i in range(points)]
    cache_set(key, prices, SPARK_TTL)
    return prices


# ---------------- 场外基金: 东财官方净值(每日盘后更新) ----------------
# 注: 盘中估值接口(fundgz)已于2024年被监管叫停,全网失效。
# 场外基金只能拿到最新官方净值+日涨幅;要盘中实时请改用场内ETF(如 sh510300)。
# http://api.fund.eastmoney.com/f10/lsjz?fundCode=005827&pageIndex=1&pageSize=1
FUND_TTL = 1800  # 净值每日只更新一次,缓存30分钟


def fetch_fund(code):
    key = f"f:{code}"
    hit = cache_get(key)
    if hit:
        return hit
    r = requests.get(
        "http://api.fund.eastmoney.com/f10/lsjz",
        params={"fundCode": code, "pageIndex": 1, "pageSize": 1},
        headers={**UA, "Referer": "http://fundf10.eastmoney.com/"},
        timeout=8)
    rows = (r.json().get("Data") or {}).get("LSJZList") or []
    if not rows:
        return None
    row = rows[0]
    nav = float(row["DWJZ"])
    pct = float(row["JZZZL"] or 0)
    data = {
        "symbol": f"f{code}",
        "name": f"{code} NAV {row['FSRQ'][5:]}",  # 例: "005827 NAV 07-31"
        "price": nav,
        "change": round(nav - nav / (1 + pct / 100), 4),
        "changePct": pct,
        "ok": True,
    }
    cache_set(key, data, FUND_TTL)
    return data


# ---------------- HTTP 接口 ----------------
@app.route("/quote")
def quote():
    symbol = request.args.get("symbol", "").strip()
    points = int(request.args.get("points", 48))
    if not symbol:
        return jsonify({"ok": False, "err": "no symbol"}), 400
    try:
        if symbol.startswith("f") and symbol[1:].isdigit():
            data = fetch_fund(symbol[1:])
            if data:
                data["spark"] = []          # 场外基金无分时
        else:
            data = fetch_stock(symbol)
            if data:
                data["spark"] = fetch_spark(symbol, points)
    except Exception as e:
        return jsonify({"ok": False, "err": str(e)}), 502
    if not data:
        return jsonify({"ok": False, "err": "not found"}), 404
    return jsonify(data)


@app.route("/health")
def health():
    return jsonify({"ok": True, "cached": len(_cache)})


if __name__ == "__main__":
    print("stock-tv daemon on http://0.0.0.0:8899")
    print("test: http://127.0.0.1:8899/quote?symbol=sh600519")
    app.run(host="0.0.0.0", port=8899)
