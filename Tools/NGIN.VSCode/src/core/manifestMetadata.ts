import { readFileSync } from 'node:fs';

export interface MetadataAttribute {
  name: string;
  type: string;
  required: boolean;
  values?: string[];
  documentation?: string;
}

export interface MetadataChild {
  id: string;
  min: number;
  max: number | null;
}

export interface MetadataElement {
  id: string;
  name: string;
  namespace: string;
  documentation: string;
  attributes: MetadataAttribute[];
  children: MetadataChild[];
}

export interface ManifestEditorMetadata {
  documents: Array<{ kind: string; extension: string; roots: string[]; schema: string }>;
  namespaces: Array<{ uri: string; prefix: string }>;
  elements: MetadataElement[];
}

export function documentRoots(metadata: ManifestEditorMetadata, kind: string): readonly string[] {
  return metadata.documents.find(document => document.kind === kind)?.roots ?? [];
}

export function loadManifestMetadata(file: string): ManifestEditorMetadata {
  return JSON.parse(readFileSync(file, 'utf8')) as ManifestEditorMetadata;
}

export function metadataElement(metadata: ManifestEditorMetadata, id: string): MetadataElement {
  const element = metadata.elements.find(candidate => candidate.id === id);
  if (!element) throw new Error(`NGIN editor metadata is missing ${id}.`);
  return element;
}

export function attributeChoices(
  metadata: ManifestEditorMetadata,
  elementId: string,
  attributeName: string
): readonly string[] {
  return metadataElement(metadata, elementId).attributes.find(attribute => attribute.name === attributeName)?.values ?? [];
}

export function childElementNames(metadata: ManifestEditorMetadata, elementId: string): string[] {
  return metadataElement(metadata, elementId).children.map(child => metadataElement(metadata, child.id).name);
}
