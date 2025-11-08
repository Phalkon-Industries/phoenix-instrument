# scan.py
import asyncio
from bleak import BleakScanner

async def main():
    print("Scanning 5s…")
    devices = await BleakScanner.discover(timeout=5.0)
    for d in devices:
        print(d)

if __name__ == "__main__":
    asyncio.run(main())
