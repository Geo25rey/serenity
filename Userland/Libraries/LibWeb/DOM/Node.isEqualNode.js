// https://dom.spec.whatwg.org/#dom-node-isequalnode
function Node_isEqualNode(other) {
    if (!(this instanceof Node)) {
        throw TypeError("isEqualNode on not-a-node!");
    }

    // The isEqualNode(otherNode) method steps are to return true if otherNode is non-null and this equals otherNode; otherwise false.
    if (other === null) {
        return false;
    }

    // Fast path for testing a node against itself.
    if (this === other) {
        return true;
    }

    if (!(other instanceof Node)) {
        throw TypeError("isEqualNode arg is not-a-node!");
    }

    // https://dom.spec.whatwg.org/#concept-node-equals
    // A node A equals a node B if all of the following conditions are true:

    // A and B implement the same interfaces.
    if (this.nodeName !== other.nodeName) {
        return false;
    }

    switch (this.nodeType) {
        // DocumentType
        case Node.DOCUMENT_TYPE_NODE:
            // Its name, public ID, and system ID.
            if (
                this.name !== other.name ||
                this.publicId !== other.publicId ||
                this.systemId !== other.systemId
            ) {
                return false;
            }
            break;
        // Element
        case Node.ELEMENT_NODE:
            // Its namespace, namespace prefix, local name, and its attribute list’s size.
            if (
                this.namespace !== other.namespaces ||
                this.prefix !== other.prefix ||
                this.localName !== other.localName ||
                this.attributes.length !== other.attributes.length
            ) {
                return false;
            }
            break;
        case Node.COMMENT_NODE:
        case Node.TEXT_NODE:
            if (this.data !== other.data) {
                return false;
            }
            break;
        case Node.ATTRIBUTE_NODE:
            if (
                this.namespaceURI !== other.namespaceURI ||
                this.localName !== other.localName ||
                this.value !== other.value
            ) {
                return false;
            }
            break;
        case Node.PROCESSING_INSTRUCTION_NODE:
            if (this.target !== other.target || this.data !== other.data) return false;
            break;
        default:
            break;
    }

    // If A is an element, each attribute in its attribute list has an attribute that equals an attribute in B’s attribute list.
    for (let i = 0; i < this.attributes.length; ++i) {
        let attr = this.attributes[i];
        if (!attr.isEqualNode(other.getAttributeNodeNS(attr.namespaceURI, attr.name))) {
            return false;
        }
    }

    // A and B have the same number of children.
    let childCount = this.childNodes.length;
    if (childCount !== other.childNodes.length) return false;

    // Each child of A equals the child of B at the identical index.
    for (let i = 0; i < childCount; ++i) {
        let child = this.childNodes[i];
        if (!child.isEqualNode(other.childNodes[i])) return false;
    }

    return true;
}
