"""
	test_mcp_client.py -- official Python SDK client integration test for the
	StarGen MCP server (legacy initialize-handshake era, which is what the
	current SDK speaks).

	Runs the server as a subprocess, performs the handshake, lists tools,
	and calls list_stars / generate_system / find_systems.
"""

import asyncio
import json
import os
import sys

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


async def main() -> int:
	exe = sys.argv[1]
	params = StdioServerParameters(command=exe, args=["--mcp"])

	failures = 0

	async with stdio_client(params) as (read, write):
		async with ClientSession(read, write) as session:
			info = await session.initialize()
			print("initialize:", info.server_info.name, info.protocol_version)
			assert info.server_info.name == "stargen"

			tools = await session.list_tools()
			names = sorted(t.name for t in tools.tools)
			print("tools:", names)
			assert names == ["find_systems", "generate_system", "list_stars"]

			# list_stars
			result = await session.call_tool("list_stars", {"catalog": "dole"})
			assert not result.is_error
			data = result.structured_content
			assert data["star_count"] == 16
			print("list_stars: OK,", data["star_count"], "stars")

			# generate_system (summary)
			result = await session.call_tool(
				"generate_system",
				{"mass_solar": 1.0, "seed": 42, "detail": "summary"},
			)
			assert not result.is_error
			sc = result.structured_content
			assert sc["effective"]["seed"] == 42
			assert sc["summary"]["planet_count"] == 11
			print("generate_system: OK,", sc["summary"]["planet_count"], "planets")

			# find_systems
			result = await session.call_tool(
				"find_systems",
				{
					"mass_solar": 1.0,
					"seed_start": 42,
					"max_attempts": 10,
					"max_results": 3,
					"criteria": {"min_habitable": 1},
				},
			)
			assert not result.is_error
			fs = result.structured_content
			assert fs["attempts"] >= 1
			assert all(m["summary"]["habitable_planets"] >= 1 for m in fs["matches"])
			print("find_systems: OK,", fs["attempts"], "attempts,",
				  len(fs["matches"]), "habitable matches")

			# input error -> tool execution error (isError true, per the
			# MCP tools spec: models can self-correct)
			result = await session.call_tool(
				"generate_system", {"mass_solar": 9.9}
			)
			assert result.is_error
			assert "outside the supported range" in result.content[0].text
			print("input validation: OK (isError tool result)")

			print("SDK CLIENT TEST PASSED")

	return 0


if __name__ == "__main__":
	sys.exit(asyncio.run(main()))
