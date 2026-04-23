#!/usr/bin/env node

/*
 * Playwright MCP stdio proxy that strips $schema from tool input schemas.
 *
 * Why: Some clients fail to validate tool schemas that include top-level
 * "$schema": "https://json-schema.org/draft/2020-12/schema".
 *
 * Upstream context: microsoft/playwright#40043.
 */

const { spawn } = require('child_process');

const PLAYWRIGHT_MCP_VERSION = process.env.PLAYWRIGHT_MCP_VERSION || '0.0.70';

function sanitizeSchemaObject(value) {
    if (!value || typeof value !== 'object') {
        return;
    }
    if (Object.prototype.hasOwnProperty.call(value, '$schema')) {
        delete value.$schema;
    }
    for (const key of Object.keys(value)) {
        sanitizeSchemaObject(value[key]);
    }
}

function sanitizeMessage(message) {
    if (!message || typeof message !== 'object') {
        return message;
    }

    // tools/list response shape
    if (message.result && Array.isArray(message.result.tools)) {
        for (const tool of message.result.tools) {
            if (tool && tool.inputSchema && typeof tool.inputSchema === 'object') {
                sanitizeSchemaObject(tool.inputSchema);
            }
        }
    }

    return message;
}

class McpFrameParser {
    constructor(onMessage) {
        this._onMessage = onMessage;
        this._buffer = Buffer.alloc(0);
    }

    push(chunk) {
        this._buffer = Buffer.concat([this._buffer, chunk]);

        while (true) {
            const headerEnd = this._buffer.indexOf('\r\n\r\n');
            if (headerEnd === -1) {
                return;
            }

            const header = this._buffer.slice(0, headerEnd).toString('utf8');
            const lengthMatch = header.match(/Content-Length:\s*(\d+)/i);
            if (!lengthMatch) {
                throw new Error('Invalid MCP frame: missing Content-Length header');
            }

            const contentLength = Number.parseInt(lengthMatch[1], 10);
            const frameStart = headerEnd + 4;
            const frameEnd = frameStart + contentLength;
            if (this._buffer.length < frameEnd) {
                return;
            }

            const payload = this._buffer.slice(frameStart, frameEnd).toString('utf8');
            this._buffer = this._buffer.slice(frameEnd);
            this._onMessage(payload);
        }
    }
}

function encodeFrame(jsonText) {
    const payload = Buffer.from(jsonText, 'utf8');
    const header = Buffer.from(`Content-Length: ${payload.length}\r\n\r\n`, 'utf8');
    return Buffer.concat([header, payload]);
}

const child = spawn('npx', [`@playwright/mcp@${PLAYWRIGHT_MCP_VERSION}`], {
    stdio: ['pipe', 'pipe', 'pipe'],
    env: process.env,
});

child.stderr.on('data', chunk => process.stderr.write(chunk));
child.on('error', err => {
    process.stderr.write(`playwright-schema-proxy: child process error: ${err.message}\n`);
    process.exit(1);
});
child.on('exit', code => {
    process.exit(code == null ? 1 : code);
});

const clientToServer = new McpFrameParser(payload => {
    child.stdin.write(encodeFrame(payload));
});

const serverToClient = new McpFrameParser(payload => {
    try {
        const message = JSON.parse(payload);
        const sanitized = sanitizeMessage(message);
        process.stdout.write(encodeFrame(JSON.stringify(sanitized)));
    } catch {
        // If payload is non-JSON for any reason, forward it untouched.
        process.stdout.write(encodeFrame(payload));
    }
});

process.stdin.on('data', chunk => {
    try {
        clientToServer.push(chunk);
    } catch (err) {
        process.stderr.write(`playwright-schema-proxy: parse error (client->server): ${err.message}\n`);
    }
});

child.stdout.on('data', chunk => {
    try {
        serverToClient.push(chunk);
    } catch (err) {
        process.stderr.write(`playwright-schema-proxy: parse error (server->client): ${err.message}\n`);
    }
});

process.stdin.on('end', () => child.stdin.end());
