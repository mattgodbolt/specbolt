import _ from "lodash";

const ArchiveOrgCors = "https://cors.archive.org";

type IAFile = {
    name: string;
} | Record<string, string>;

type IAMetadata = {
    identifier: string;
    collection: string[];
    description: string;
    title: string;
    emulator: string;
    emulator_ext: string;
} | Record<string, string>;

type IAMetadataResponse = {
    created: number;
    dir: string;
    files: IAFile[];
    metadata: IAMetadata;
};

async function findSpectrumZ80Files(itemId: string) {
    const metadataUrl = `${ArchiveOrgCors}/metadata/${itemId}`;
    const response = await fetch(metadataUrl);
    const data: IAMetadataResponse = await response.json();

    const extension = `.${data.metadata.emulator_ext}`;
    return data.files.filter((file) => file.name.toLowerCase().endsWith(extension));
}

function getCorsFileDownloadUrl(itemId: string, fileName: string) {
    return `${ArchiveOrgCors}/cors/${itemId}/${fileName}`;
}

export async function downloadSnapshotUrlFor(itemId: string): Promise<string | undefined> {
    const files = await findSpectrumZ80Files(itemId);
    if (files.length === 1) {
        return getCorsFileDownloadUrl(itemId, files[0].name);
    }
    return undefined;
}

async function _searchForSpectrum(query: string): Promise<{ identifier: string; title: string }[]> {
    if (query.length <= 1) return [];
    const searchQuery = encodeURI(`title:(${query}) AND collection:(softwarelibrary_zx_spectrum)`);
    const searchUrl = `${ArchiveOrgCors}/advancedsearch.php?q=${searchQuery}&fl[]=identifier,title&output=json`;
    const response = await fetch(searchUrl);
    const data = await response.json();
    if (!data.response) return [];
    return data.response.docs;
}

export const searchForSpectrum = _.memoize(_searchForSpectrum);
