package main

// -set-models: install.sh writes the models the user picked into an existing config.yaml without touching the
// rest of it (API keys, tunnel token, comments): the "models:" list is swapped for the one in another YAML file,
// and the old file is kept as config.yaml.bak.

import (
	"bytes"
	"fmt"
	"os"

	"gopkg.in/yaml.v3"
)

func setModels(cfgPath, modelsPath string) error {
	raw, err := os.ReadFile(cfgPath)
	if err != nil {
		return err
	}
	var doc yaml.Node
	if err := yaml.Unmarshal(raw, &doc); err != nil {
		return fmt.Errorf("%s: %w", cfgPath, err)
	}
	mraw, err := os.ReadFile(modelsPath)
	if err != nil {
		return err
	}
	var mdoc yaml.Node
	if err := yaml.Unmarshal(mraw, &mdoc); err != nil {
		return fmt.Errorf("%s: %w", modelsPath, err)
	}
	// the new list: either a bare sequence or a document with a "models:" key
	newList := mdoc.Content[0]
	if newList.Kind == yaml.MappingNode {
		newList = mapValue(newList, "models")
	}
	if newList == nil || newList.Kind != yaml.SequenceNode {
		return fmt.Errorf("%s: expected a list of models", modelsPath)
	}
	root := doc.Content[0]
	if root.Kind != yaml.MappingNode {
		return fmt.Errorf("%s: not a mapping", cfgPath)
	}
	if v := mapValue(root, "models"); v != nil {
		*v = *newList
	} else {
		root.Content = append(root.Content, &yaml.Node{Kind: yaml.ScalarNode, Value: "models"}, newList)
	}
	var out bytes.Buffer
	enc := yaml.NewEncoder(&out)
	enc.SetIndent(2)
	if err := enc.Encode(&doc); err != nil {
		return err
	}
	if err := os.WriteFile(cfgPath+".bak", raw, 0o600); err != nil {
		return err
	}
	return os.WriteFile(cfgPath, out.Bytes(), 0o600)
}

func mapValue(m *yaml.Node, key string) *yaml.Node {
	for i := 0; i+1 < len(m.Content); i += 2 {
		if m.Content[i].Value == key {
			return m.Content[i+1]
		}
	}
	return nil
}
