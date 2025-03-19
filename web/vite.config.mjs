import fs from 'fs';
import path from 'path';
import {defineConfig, loadEnv} from "vite";

/** @type {import("vite").UserConfig} */
export default function({mode}) {
    process.env = {...process.env, ...loadEnv(mode, process.cwd())};

    const build_dir = process.env.VITE_WASM_BUILD_DIR;
    if (!build_dir) {
        throw new Error(
            "VITE_WASM_BUILD_DIR environment variable not set. Use vite-dev.sh and supply it, or put it in `.env.local`");
    }
    if (!fs.lstatSync(build_dir).isDirectory()) {
        throw new Error("VITE_WASM_BUILD_DIR environment variable set to non-existent directory " + build_dir);
    }

    const spectrumWasm = path.resolve(`${build_dir}/web/spectrum.wasm`);

    return defineConfig({
        build : {
            sourcemap :
                true, // Prevent inlining; we don't want any worklets/audio workers to be inlined as that doesn't work.
            assetsInlineLimit : 0,
        },
        resolve : {
            alias : {
                '@spectrum.wasm?url' : `${spectrumWasm}?url`,
            }
        },
    });
}
