-- This is just for running "unit-test" (i.e. the main method that check things)
CREATE TABLE openapi_demo
(
    id          INTEGER NOT NULL,
    description VARCHAR(200),
    large_value BIGINT,
    event_time  TIMESTAMP WITHOUT TIME ZONE,
    some_date   INGRESDATE
);