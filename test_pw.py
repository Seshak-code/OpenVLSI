import asyncio
from playwright.async_api import async_playwright

async def main():
    async with async_playwright() as p:
        browser = await p.chromium.launch()
        page = await browser.new_page()

        # Listen to all console logs
        page.on("console", lambda msg: print(f"[{msg.type}] {msg.text}"))

        print("Navigating to http://localhost:8080/AcuteSim.html...")
        await page.goto("http://localhost:8080/AcuteSim.html")
        
        print("Waiting 5 seconds for initialization...")
        await asyncio.sleep(5)

        print("Done.")
        await browser.close()

asyncio.run(main())
