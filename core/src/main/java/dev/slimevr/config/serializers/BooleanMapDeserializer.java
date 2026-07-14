package dev.slimevr.config.serializers;

import com.fasterxml.jackson.core.JsonParser;
import com.fasterxml.jackson.databind.DeserializationContext;
import com.fasterxml.jackson.databind.JsonDeserializer;
import com.fasterxml.jackson.databind.JsonNode;
import java.io.IOException;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;

public class BooleanMapDeserializer extends JsonDeserializer<Map<String, Boolean>> {
	@Override
	public Map<String, Boolean> deserialize(JsonParser p, DeserializationContext ctxt) throws IOException {
		JsonNode node = p.getCodec().readTree(p);
		Map<String, Boolean> map = new HashMap<>();
		if (node.isObject()) {
			Iterator<Map.Entry<String, JsonNode>> fields = node.fields();
			while (fields.hasNext()) {
				Map.Entry<String, JsonNode> entry = fields.next();
				map.put(entry.getKey(), entry.getValue().asBoolean());
			}
		}
		return map;
	}
}
