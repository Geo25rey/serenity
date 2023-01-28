function Node_isSameNode(other) {
    if (this instanceof Node === false) {
        throw TypeError("isSameNode on not-a-node!");
    }
    if (other instanceof Node === false) {
        throw TypeError("isSameNode arg is not-a-node!");
    }

    return this === other;
}
